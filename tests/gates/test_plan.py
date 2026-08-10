#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""plan.py 排程器測試（派工來源 + G6 停點鏈路的上半段）。

`plan.py` 不是「閘門」，但 AGENTS.md §3 說**工作單元只能來自它**，
而 §4 說**合併與否由 `plan.py gate` 查表決定**。它算錯的後果不是紅燈，
是**派出一個相依還沒好、或與別人搶同一片檔案的工作單元**——
那種錯不會有任何東西擋，只會在兩個 subagent 同時改同一個檔時才發現。

所以本檔的重點是三件事：
1. **相依未滿足者不得出現在可派工批次**（漏擋 = 派出注定失敗的工）
2. **寫入範圍衝突者不得同時派**（漏擋 = 兩個 agent 互相覆蓋）
3. **`autonomy: halt` 的單元不得自動合併**，而且要在呼叫 halt_gate 之前就攔下

可直接執行：`python3 tests/gates/test_plan.py`
"""
import os
import subprocess
import sys
import unittest

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(REPO_ROOT, "scripts")
PLAN_PATH = os.path.join(SCRIPTS, "plan.py")

sys.path.insert(0, SCRIPTS)
import plan  # noqa: E402


def unit(uid, deps=(), scope=None, wave=1, lb=0, risk="low", autonomy=None):
    return {"id": uid, "title": uid + " 標題", "priority": "P0", "group": "核心",
            "layer": "engine", "stage": "A", "module": uid.split("-")[0],
            "primitives": ["K1"], "depends_on": list(deps), "wave": wave,
            "load_bearing": lb, "risk": risk, "autonomy": autonomy,
            "write_scope": list(scope or ["engine/%s/**" % uid.lower().replace("-", "_")]),
            "branch": "feat/" + uid.lower().replace("-", "_"),
            "acceptance": "單元測試綠燈", "platform": "neutral", "phase": 1}


class FakeUnits:
    """暫時把 plan 的模組級 M / U 換掉。

    plan.py 在 import 時就讀死了真實 units.json，沒有注入點——
    這個 context manager 是最小侵入的替代方案（不改被測程式碼的結構）。
    """

    def __init__(self, units):
        self.units = units

    def __enter__(self):
        self._M, self._U = plan.M, plan.U
        plan.M = {"units": self.units}
        plan.U = {u["id"]: u for u in self.units}
        return self

    def __exit__(self, *exc):
        plan.M, plan.U = self._M, self._U


def st(done=(), in_progress=()):
    return {"done": list(done), "in_progress": {i: {} for i in in_progress}}


class TestDependencies(unittest.TestCase):
    def test_unmet_dependency_is_not_dispatchable(self):
        units = [unit("E1-01"), unit("E1-02", deps=["E1-01"])]
        with FakeUnits(units):
            self.assertEqual([u["id"] for u in plan.ready(st())], ["E1-01"])

    def test_met_dependency_becomes_dispatchable(self):
        units = [unit("E1-01"), unit("E1-02", deps=["E1-01"])]
        with FakeUnits(units):
            self.assertEqual([u["id"] for u in plan.ready(st(done=["E1-01"]))], ["E1-02"])

    def test_partially_met_dependencies_still_blocked(self):
        units = [unit("E1-01"), unit("E1-02"), unit("E1-03", deps=["E1-01", "E1-02"])]
        with FakeUnits(units):
            got = [u["id"] for u in plan.ready(st(done=["E1-01"]))]
        self.assertNotIn("E1-03", got)

    def test_done_units_are_excluded(self):
        with FakeUnits([unit("E1-01")]):
            self.assertEqual(plan.ready(st(done=["E1-01"])), [])

    def test_in_progress_units_are_excluded(self):
        with FakeUnits([unit("E1-01")]):
            self.assertEqual(plan.ready(st(in_progress=["E1-01"])), [])


class TestScopeConflict(unittest.TestCase):
    """兩個單元不得同時持有同一片寫入範圍——漏擋不會紅燈，只會互相覆蓋。"""

    def test_conflict_with_in_progress_unit_is_excluded(self):
        units = [unit("E1-01", scope=["engine/shared/**"]),
                 unit("E1-02", scope=["engine/shared/**"])]
        with FakeUnits(units):
            got = [u["id"] for u in plan.ready(st(in_progress=["E1-01"]))]
        self.assertEqual(got, [])

    def test_conflict_within_the_same_batch_takes_only_one(self):
        units = [unit("E1-01", scope=["engine/shared/**"]),
                 unit("E1-02", scope=["engine/shared/**"]),
                 unit("E1-03", scope=["engine/other/**"])]
        with FakeUnits(units):
            got = [u["id"] for u in plan.ready(st())]
        self.assertEqual(len(got), 2)
        self.assertIn("E1-03", got)
        self.assertTrue(("E1-01" in got) != ("E1-02" in got), "同範圍只能出一個")

    def test_disjoint_scopes_all_dispatchable(self):
        units = [unit("E1-01"), unit("E1-02"), unit("E1-03")]
        with FakeUnits(units):
            self.assertEqual(len(plan.ready(st())), 3)


class TestOrderingAndLimit(unittest.TestCase):
    def test_sorted_by_wave_then_load_bearing_desc(self):
        units = [unit("E1-03", wave=2, lb=9), unit("E1-01", wave=1, lb=1),
                 unit("E1-02", wave=1, lb=5)]
        with FakeUnits(units):
            self.assertEqual([u["id"] for u in plan.ready(st())],
                             ["E1-02", "E1-01", "E1-03"])

    def test_limit_is_respected(self):
        units = [unit("E1-0%d" % i) for i in range(1, 6)]
        with FakeUnits(units):
            self.assertEqual(len(plan.ready(st(), limit=2)), 2)

    def test_limit_none_returns_all(self):
        units = [unit("E1-0%d" % i) for i in range(1, 6)]
        with FakeUnits(units):
            self.assertEqual(len(plan.ready(st())), 5)


class TestUnitForBranch(unittest.TestCase):
    def test_feat_slug_resolves(self):
        with FakeUnits([unit("E1-01")]):
            self.assertEqual(plan.unit_for_branch("feat/e1_01")["id"], "E1-01")

    def test_full_ref_resolves(self):
        with FakeUnits([unit("E1-01")]):
            self.assertEqual(plan.unit_for_branch("refs/heads/feat/e1_01")["id"], "E1-01")

    def test_unknown_branch_exits_1(self):
        # CI 的 unit 步驟靠這個 exit 1 變紅——回 None 會讓後面的閘門拿空 id 繼續跑。
        with FakeUnits([unit("E1-01")]):
            with self.assertRaises(SystemExit) as cm:
                plan.unit_for_branch("feat/does_not_exist")
        self.assertEqual(cm.exception.code, 1)

    def test_empty_ref_exits_1(self):
        with FakeUnits([unit("E1-01")]):
            with self.assertRaises(SystemExit) as cm:
                plan.unit_for_branch("")
        self.assertEqual(cm.exception.code, 1)


class TestBrief(unittest.TestCase):
    """派工簡報是 subagent 的唯一輸入，AGENTS.md 要求原文轉交、不得摘要。"""

    def test_brief_carries_the_four_keys(self):
        with FakeUnits([unit("E1-01", scope=["engine/e1_01/**", "tests/e1/test_e1_01*"])]):
            text = plan.brief("E1-01", n=7)
        self.assertIn("I1.7", text)
        self.assertIn("feat/e1_01", text)          # 1 分支
        self.assertIn("engine/e1_01/**", text)     # 2 結構位置 + 4 鎖定範圍
        self.assertIn("E1-01", text)               # 3 需求切片
        for s in ["engine/e1_01/**", "tests/e1/test_e1_01*"]:
            self.assertIn(s, text)

    def test_medium_risk_forbids_self_acceptance(self):
        with FakeUnits([unit("E1-01", risk="medium")]):
            self.assertIn("V1 獨立驗收", plan.brief("E1-01"))

    def test_low_risk_allows_inline_self_check(self):
        with FakeUnits([unit("E1-01", risk="low")]):
            self.assertIn("inline 自驗", plan.brief("E1-01"))

    def test_halt_autonomy_is_stated_in_brief(self):
        with FakeUnits([unit("E1-01", autonomy="halt")]):
            self.assertIn("Autonomy: halt", plan.brief("E1-01"))


class TestGateSubcommand(unittest.TestCase):
    def test_halt_override_short_circuits(self):
        """`autonomy: halt` 必須在呼叫 halt_gate 之前就攔下。

        用一個**不存在的** halt_gate 路徑跑：若 plan.py 真的去呼叫它就會壞，
        所以「仍然乾淨地回 10」證明了它根本沒走到那一步。
        """
        env = dict(os.environ)
        env["AI_SDLC_HALT_GATE"] = os.path.join(SCRIPTS, "no_such_halt_gate.py")
        halted = [u["id"] for u in plan.M["units"] if u.get("autonomy") == "halt"]
        if not halted:
            self.skipTest("units.json 目前沒有 autonomy: halt 的單元")
        proc = subprocess.run(
            [sys.executable, PLAN_PATH, "gate", halted[0],
             "--gate", "before_merge_or_release"],
            cwd=REPO_ROOT, env=env, capture_output=True, text=True,
            encoding="utf-8", errors="replace")
        self.assertEqual(proc.returncode, 10, proc.stdout + proc.stderr)
        self.assertIn("HALT (unit override)", proc.stdout)

    def test_low_risk_unit_is_auto(self):
        low = next((u["id"] for u in plan.M["units"]
                    if u["risk"] == "low" and not u.get("autonomy")), None)
        self.assertIsNotNone(low, "units.json 應該有 low 風險單元")
        proc = subprocess.run(
            [sys.executable, PLAN_PATH, "gate", low, "--gate", "before_merge_or_release"],
            cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace")
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_medium_risk_unit_halts(self):
        med = next((u["id"] for u in plan.M["units"]
                    if u["risk"] == "medium" and not u.get("autonomy")), None)
        self.assertIsNotNone(med, "units.json 應該有 medium 風險單元")
        proc = subprocess.run(
            [sys.executable, PLAN_PATH, "gate", med, "--gate", "before_merge_or_release"],
            cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace")
        self.assertEqual(proc.returncode, 10, proc.stdout + proc.stderr)


class TestRealUnitsFile(unittest.TestCase):
    def test_every_dependency_exists(self):
        # 相依指向不存在的 id → ready() 永遠不會放行該單元，而且不會有任何錯誤訊息。
        missing = [(u["id"], d) for u in plan.M["units"]
                   for d in u["depends_on"] if d not in plan.U]
        self.assertEqual(missing, [])

    def test_no_self_dependency(self):
        self.assertEqual([u["id"] for u in plan.M["units"] if u["id"] in u["depends_on"]], [])

    def test_every_branch_slug_round_trips(self):
        # unit_for_branch 靠 id.lower().replace('-','_') == slug；branch 欄位對不上就查不到。
        bad = [u["id"] for u in plan.M["units"]
               if u["branch"].rsplit("/", 1)[-1] != u["id"].lower().replace("-", "_")]
        self.assertEqual(bad, [])

    def test_brief_renders_for_every_unit(self):
        # BRIEF.format 少一個欄位就 KeyError——那會在派工當下才炸。
        for u in plan.M["units"]:
            try:
                plan.brief(u["id"])
            except Exception as exc:
                self.fail("brief(%s) 失敗：%r" % (u["id"], exc))


if __name__ == "__main__":
    unittest.main(verbosity=2)
