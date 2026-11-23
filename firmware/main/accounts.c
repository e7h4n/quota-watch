#include "quota.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

_Static_assert(sizeof(quota_card)==184, "wire v1 layout");
static struct {uint32_t count; quota_card cards[MAX_CARDS];} db;
static SemaphoreHandle_t mutex;
static nvs_handle_t handle;
static unsigned generation;
static bool printable(const char *text, size_t size) {
    size_t len=strnlen(text,size);
    if (!len || len==size) return false;
    for(size_t i=0;i<len;i++) if((unsigned char)text[i]<32 || text[i]==127) return false;
    return true;
}
static bool valid(const quota_card *c) {
    return printable(c->id,sizeof c->id) && printable(c->email,sizeof c->email) &&
        printable(c->tier,sizeof c->tier) && c->provider<=1 && !c->reserved &&
        (c->short_pct<=100 || c->short_pct==255) && (c->week_pct<=100 || c->week_pct==255) &&
        c->credit_cents>=-1 && c->resets>=-1 && c->captured>=1600000000;
}
void accounts_init(void) {
    mutex=xSemaphoreCreateMutex(); configASSERT(mutex);
    ESP_ERROR_CHECK(nvs_open("quota_watch",NVS_READWRITE,&handle));
    size_t size=sizeof db;
    esp_err_t err=nvs_get_blob(handle,"cards_v1",&db,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND) return;
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(size==sizeof db && db.count<=MAX_CARDS ? ESP_OK : ESP_ERR_INVALID_SIZE);
    for(unsigned i=0;i<db.count;i++) ESP_ERROR_CHECK(valid(&db.cards[i])?ESP_OK:ESP_ERR_INVALID_ARG);
}
unsigned accounts_read(unsigned index,quota_card *out) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    unsigned count=db.count;
    if(out && index<count) *out=db.cards[index];
    xSemaphoreGive(mutex);
    return count;
}
unsigned accounts_generation(void) {return __atomic_load_n(&generation,__ATOMIC_ACQUIRE);}
static bool change(const char *id,const quota_card *card) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    __typeof__(db) next=db;
    unsigned i=0;
    while(i<next.count && strcmp(next.cards[i].id,id)) i++;
    bool ok=false;
    if(card && i<MAX_CARDS && (i==next.count || card->captured>=next.cards[i].captured)) {
        next.cards[i]=*card;
        if(i==next.count) next.count++;
    } else if(!card && i<next.count) {
        next.count--;
        memmove(&next.cards[i],&next.cards[i+1],(next.count-i)*sizeof(quota_card));
        memset(&next.cards[next.count],0,sizeof(quota_card));
    } else goto done;
    ok=nvs_set_blob(handle,"cards_v1",&next,sizeof next)==ESP_OK && nvs_commit(handle)==ESP_OK;
    if(ok) {db=next; __atomic_add_fetch(&generation,1,__ATOMIC_RELEASE);}
done:
    xSemaphoreGive(mutex);
    return ok;
}
bool accounts_upsert(const quota_card *card) {return valid(card) && change(card->id,card);}
bool accounts_delete(const char *id) {return change(id,NULL);}
