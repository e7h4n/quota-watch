#!/usr/bin/env python3
"""Export read-only Quota Watch cards using the provider's local client."""
import argparse
import asyncio
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from decimal import Decimal


def card_header(provider, email, tier, identity):
    if not isinstance(email, str) or "@" not in email:
        raise ValueError("The client did not supply an account email")
    tier = str(tier or "Unknown").replace("_", " ").title()
    identity = provider + ":" + email.casefold() + ":" + (identity or "")
    return dict(version=1, provider=provider, email=email, tier=tier,
                captured=int(time.time()), id=hashlib.sha256(identity.encode()).hexdigest()[:32])


def window(data, usage_key, reset_key):
    if not data:
        return None
    used = data.get(usage_key)
    if used is not None and (not math.isfinite(used) or not 0 <= used <= 100):
        raise ValueError("Unexpected quota percentage")
    return dict(remaining=None if used is None else round(100-used), reset=data.get(reset_key))


def codex_card(account_result, usage):
    account = account_result.get("account") or {}
    if account.get("type") != "chatgpt":
        raise ValueError("Run codex login in this profile and choose Sign in with ChatGPT")
    bucket = (usage.get("rateLimitsByLimitId") or {}).get("codex") or usage["rateLimits"]
    card = card_header("ChatGPT", account["email"], account.get("planType"), usage.get("accountId"))
    windows = {row["windowDurationMins"]: window(row, "usedPercent", "resetsAt")
               for key in ("primary", "secondary") if (row := bucket.get(key)) and row.get("windowDurationMins")}
    credits = bucket.get("credits") or {}
    balance = credits.get("balance")
    card.update(short=windows.get(300), week=windows.get(10080),
                creditCents=None if balance is None or credits.get("unlimited") or not credits.get("hasCredits")
                else int((Decimal(str(balance))*100).to_integral_value()),
                resets=(usage.get("rateLimitResetCredits") or {}).get("availableCount"))
    return card


async def read_codex():
    proc = await asyncio.create_subprocess_exec("codex", "app-server", stdin=asyncio.subprocess.PIPE,
                                                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
                                                limit=2**20)
    async def send(payload):
        proc.stdin.write((json.dumps(payload)+"\n").encode())
        await proc.stdin.drain()

    async def request(i, method, params):
        await send(dict(id=i, method=method, params=params))
        while line := await proc.stdout.readline():
            msg = json.loads(line)
            if msg.get("id") == i and "method" not in msg:
                if "error" in msg:
                    raise ValueError(method+" failed. Check the official client's login and connectivity.")
                return msg["result"]
        raise ValueError("Codex app-server closed before responding")

    try:
        await request(1, "initialize", {"clientInfo":{"name":"quota_watch","version":"1.0"},
                                        "capabilities":{"experimentalApi":True}})
        await send({"method":"initialized"})
        account = await request(2, "account/read", {"refreshToken":False})
        usage = await request(3, "account/rateLimits/read", {})
        again = await request(4, "account/read", {"refreshToken":False})
        if account.get("account") != again.get("account") or account.get("userId") != again.get("userId"):
            raise ValueError("Account changed during the read. Run the export again.")
        return codex_card(account,usage)
    finally:
        if proc.returncode is None:
            proc.terminate()
        try:
            await asyncio.wait_for(proc.wait(),2)
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()


def claude_card(status, payload):
    if not status.get("loggedIn"):
        raise ValueError("Sign in using Claude Code in this profile")
    card = card_header("Claude",status.get("email"),status.get("subscriptionType"),status.get("orgId"))
    limits = payload.get("rate_limits") or {}
    card.update(short=window(limits.get("five_hour"),"used_percentage","resets_at"),
                week=window(limits.get("seven_day"),"used_percentage","resets_at"))
    return card


def write_card(path, card):
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent,delete=False) as tmp:
        json.dump(card,tmp,ensure_ascii=False)
        tmp.write("\n")
    try:
        os.replace(tmp.name,path)
    finally:
        Path(tmp.name).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("provider",choices=["claude","codex"])
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    if args.provider == "codex":
        card = asyncio.run(asyncio.wait_for(read_codex(),45))
    else:
        payload = json.load(sys.stdin)
        auth = subprocess.run(["claude","auth","status","--json"],check=True,timeout=15,
                              capture_output=True,text=True)
        card = claude_card(json.loads(auth.stdout),payload)
    write_card(args.out.expanduser(),card)
    if args.provider=="claude":
        print("Quota Watch · "+card["tier"])


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, asyncio.TimeoutError) as exc:
        sys.exit("Quota Watch export failed: "+(str(exc) or "client timed out"))
