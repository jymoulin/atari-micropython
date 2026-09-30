# Module compile par mpy-cross -march=m68k : code natif precompile.
import micropython


@micropython.native
def somme(n):
    s = 0
    for i in range(n):
        s += i
    return s


@micropython.native
def attrape(n):
    t = 0
    for i in range(n):
        try:
            if i % 2:
                raise ValueError(i)
        except ValueError as e:
            t += e.args[0]
    return t


@micropython.viper
def v_somme(buf, n: int) -> int:
    p = ptr8(buf)
    s = 0
    for i in range(n):
        s += p[i]
    return s


@micropython.viper
def v_mul(a: int, b: int) -> int:
    return a * b


@micropython.viper
def v_cmp(a: uint, b: uint) -> int:
    return int(a < b) + int(a >= b) * 10


@micropython.asm_m68k
def a_ajoute(a, b):
    movel([8, fp], d0)
    addl([12, fp], d0)
