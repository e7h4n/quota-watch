import unittest
from tools.account_card import codex_card, claude_card


class Cards(unittest.TestCase):
    def test_codex_buckets_and_identity(self):
        account = {"account":{"type":"chatgpt","email":"a@example.org","planType":"pro"}}
        usage = {"accountId":"one","rateLimits":{},"rateLimitsByLimitId":{"codex":{
            "secondary":{"windowDurationMins":300,"usedPercent":34,"resetsAt":1900000000},
            "primary":{"windowDurationMins":10080,"usedPercent":0},
            "credits":{"hasCredits":True,"balance":"12.34"}}},
            "rateLimitResetCredits":{"availableCount":0}}
        card = codex_card(account,usage)
        self.assertEqual((card["short"]["remaining"],card["week"]["remaining"],card["creditCents"],card["resets"]),(66,100,1234,0))
        self.assertNotEqual(card["id"],codex_card(account,dict(usage,accountId="two"))["id"])

    def test_claude_missing_and_full_window(self):
        auth = {"loggedIn":True,"email":"a@example.org","subscriptionType":"max"}
        card = claude_card(auth,{"rate_limits":{"five_hour":{"used_percentage":100,"resets_at":1900000000}}})
        self.assertEqual(card["short"],{"remaining":0,"reset":1900000000})
        self.assertIsNone(card["week"])
        self.assertNotIn("creditCents",card)
