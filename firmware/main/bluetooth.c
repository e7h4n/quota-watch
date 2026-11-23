#include "quota.h"
#include "oauth_settings.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_random.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);
#define QUOTA_UUID(n) BLE_UUID128_INIT(0x51,0x8a,0x43,0x2d,0xad,0x81,0xfb,0xa4,0x37,0x4c,0x18,0x3e,n,0,0,0)
static const ble_uuid128_t service=QUOTA_UUID(1), command=QUOTA_UUID(2), state=QUOTA_UUID(3);
static const ble_uuid128_t codex_id=QUOTA_UUID(4), claude_id=QUOTA_UUID(5);
#define CLIENT_ID_FLAGS (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC)
static uint8_t own_type, selected;
static quota_card staging;
static int received=-1, passkey=-1;
static bool ready;
static void code(int key) {__atomic_store_n(&passkey,key,__ATOMIC_RELEASE);}
int bluetooth_code(void) {return __atomic_load_n(&passkey,__ATOMIC_ACQUIRE);}
bool bluetooth_ready(void) {return __atomic_load_n(&ready,__ATOMIC_ACQUIRE);}

static int access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    struct ble_gap_conn_desc peer;
    if(ble_gap_conn_find(conn,&peer) || !peer.sec_state.authenticated || !peer.sec_state.encrypted)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        static uint8_t reply[2+sizeof(quota_card)];
        if(ctx->offset==0) {
            quota_card card={0};
            reply[0]=1;
            reply[1]=accounts_read(selected,&card);
            memcpy(reply+2,&card,sizeof card);
        }
        uint16_t size=reply[1]>selected ? sizeof reply : 2;
        return os_mbuf_append(ctx->om,reply,size)==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    uint8_t p[20];
    uint16_t n=0;
    if(ble_hs_mbuf_to_flat(ctx->om,p,sizeof p,&n) || n<1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    switch(p[0]) {
    case 1: // Restart a card transfer.
        if(n!=1) break;
        received=0;
        return 0;
    case 2: // Absolute byte offset makes missing or repeated fragments detectable.
        if(n<3 || received<0 || p[1]!=received || received+n-2>sizeof staging) break;
        memcpy((uint8_t*)&staging+received,p+2,n-2);
        received+=n-2;
        return 0;
    case 3:
        if(n!=1 || received!=sizeof staging) break;
        received=-1;
        return accounts_upsert(&staging) ? 0 : BLE_ATT_ERR_UNLIKELY;
    case 4:
        if(n!=2 || p[1]>=MAX_CARDS) break;
        selected=p[1];
        return 0;
    case 5: {
        if(n!=17) break;
        char id[33];
        for(int i=0;i<16;i++) snprintf(id+i*2,3,"%02x",p[1+i]);
        return accounts_delete(id) ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    case 6: {
        if(n!=5) break;
        uint32_t seconds;
        memcpy(&seconds,p+1,4);
        if(seconds<1600000000) break;
        struct timeval tv={.tv_sec=seconds};
        return settimeofday(&tv,NULL)==0 ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    }
    received=-1;
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&command.u,.access_cb=access,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&state.u,.access_cb=access,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN},
        {.uuid=&codex_id.u,.access_cb=client_id_access,.arg=(void*)0,.flags=CLIENT_ID_FLAGS},
        {.uuid=&claude_id.u,.access_cb=client_id_access,.arg=(void*)1,.flags=CLIENT_ID_FLAGS},
        {0}}},{0}
};
static int gap(struct ble_gap_event *e,void *arg);
static void advertise(void) {
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    int rc=ble_gap_adv_start(own_type,NULL,BLE_HS_FOREVER,&params,gap,NULL);
    if(rc) ESP_LOGE("quota","Advertising failed: %d",rc);
    __atomic_store_n(&ready,!rc,__ATOMIC_RELEASE);
}
static int gap(struct ble_gap_event *e,void *arg) {
    switch(e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(e->connect.status) advertise();
        else if(ble_gap_security_initiate(e->connect.conn_handle))
            ble_gap_terminate(e->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if(e->passkey.params.action!=BLE_SM_IOACT_DISP) break;
        struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=esp_random()%1000000};
        code(io.passkey);
        if(ble_sm_inject_io(e->passkey.conn_handle,&io)) ble_gap_terminate(e->passkey.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        code(-1);
        if(e->enc_change.status) ble_gap_terminate(e->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        client_id_cancel();
        received=-1; selected=0; code(-1); advertise();
        break;
    }
    return 0;
}
static void sync_host(void) {
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0,&own_type));
    struct ble_hs_adv_fields fields={0},response={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    // A passive scan should see the product name without requesting a scan response.
    static uint8_t name[]=CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME;
    fields.name=name;
    fields.name_len=sizeof(name)-1;
    fields.name_is_complete=1;
    ESP_ERROR_CHECK(ble_gap_adv_set_fields(&fields));
    response.uuids128=(ble_uuid128_t*)&service;
    response.num_uuids128=1;
    response.uuids128_is_complete=1;
    ESP_ERROR_CHECK(ble_gap_adv_rsp_set_fields(&response));
    advertise();
}
static void reset_host(int reason) {
    client_id_cancel();
    received=-1; selected=0; code(-1);
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE);
}
static void host(void *arg) {nimble_port_run();nimble_port_freertos_deinit();}
void bluetooth_start(void) {
    ESP_ERROR_CHECK(nimble_port_init());
    ble_store_config_init(); ble_svc_gap_init(); ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services)); ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_mitm=1; ble_hs_cfg.sm_sc=1; ble_hs_cfg.sm_sc_only=1; ble_hs_cfg.sm_bonding=0;
    ble_hs_cfg.sync_cb=sync_host; ble_hs_cfg.reset_cb=reset_host;
    nimble_port_freertos_init(host);
}
