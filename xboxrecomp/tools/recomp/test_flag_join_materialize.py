"""
Self-check for flag materialization at joins (translator._join_flag_needs).

Two predecessors reach a jcc having set the flags with different instructions:

        test ecx, ecx
        jz   L1
        mov  eax, ecx
        dec  eax            ; ZF from the decrement
        jmp  J
  L1:   cmp  eax, ebx       ; ZF from a comparison
  J:    jne  skip           ; <-- the states do not merge
        inc  edx
  skip: ret

The states cannot be merged, so the jne used to lift to the `_flags` fallback,
which nothing assigns: never taken. Def Jam: Fight for NY's APT request
constructor (0x00147430) has this shape in front of its `jne` over the free,
and freed every file request as it created it -- the front end never loaded a
movie and the game sat on its first loading screen.

Each predecessor knows its own flags, so each stores the jne's condition into
_flags on its edge into the join.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.recomp import config  # noqa: E402
from tools.recomp.translator import FunctionTranslator  # noqa: E402

BASE = 0x00010000


def _translate(image):
    config._install(
        [config.Section(".text", BASE, len(image), 0x0000, len(image), True)],
        entry_point=BASE, kernel_thunk_addr=BASE, origin="flag-join-materialize-test")
    db = {BASE: {"start": f"0x{BASE:08X}", "end": BASE + len(image),
                 "_addr": BASE, "size": len(image)}}
    return FunctionTranslator(image, db).translate_function(BASE, db[BASE])


IMAGE = (b"\x85\xC9"          # +0  test ecx, ecx
         b"\x74\x05"          # +2  jz +5  -> +9
         b"\x89\xC8"          # +4  mov eax, ecx
         b"\x48"              # +6  dec eax
         b"\xEB\x02"          # +7  jmp +2 -> +11
         b"\x39\xD8"          # +9  cmp eax, ebx
         b"\x75\x01"          # +11 jne +1 -> +14
         b"\x42"              # +13 inc edx
         b"\xC3")             # +14 ret


def test_each_predecessor_stores_the_condition():
    code = _translate(IMAGE)
    join = "flags for the join at 0x%08X" % (BASE + 11)
    assert code.count(join) == 2, code
    # The dec path asks its result snapshot (zero test), the cmp path the
    # comparison of its operands.
    stores = [ln for ln in code.splitlines() if join in ln]
    assert any("(_fa != 0)" in ln for ln in stores), code
    assert any("CMP_NE(_fa, _fb)" in ln for ln in stores), code


def test_switch_off_keeps_the_old_output():
    os.environ["RECOMP_FLAG_JOIN"] = "0"
    try:
        code = _translate(IMAGE)
    finally:
        del os.environ["RECOMP_FLAG_JOIN"]
    assert "flags for the join" not in code, code


if __name__ == "__main__":
    test_each_predecessor_stores_the_condition()
    test_switch_off_keeps_the_old_output()
    print("ok")
