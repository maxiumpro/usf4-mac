#!/usr/bin/env python3
"""Compara dos volcados del oraculo (CSV de sf4e__Oracle).

Uso:  python comparar_hashes.py A.csv B.csv [--estricto] [--todo] [--cola N]
                                           [--sin-rng] [--prefijo] [--frame-esperado N[-M]]

Resultados (ultima linea "RESULTADO: ...") y codigo de salida:
  IGUALES (campos: ...)   0  mismas claves (round,cursor), mismas filas por clave, y los campos
                             NOMBRADOS son iguales. No dice nada de campos no listados ni de
                             "determinismo del original".
  DIFERENTES              1  hay una diferencia: se da la primera (round,cursor) y los campos.
  INCOMPLETO              3  uno de los volcados se corta (menos rondas o cursores que el otro,
                             o la ultima ronda no llega a su cola final).
  COBERTURA DISTINTA      4  las claves (round,cursor) no coinciden y no es solo un corte.
  (error de uso/formato)  2
  control negativo fallido 6  (solo con --frame-esperado: la diferencia no cae donde se declaro)
  Con --sin-rng o --prefijo el resultado es DIAGNOSTICO, no valido como resultado: un
  "IGUALES" asi sale con codigo 5, nunca 0.

Alineacion: por (round, cursor), no por numero de fila. Para cada clave se guardan TODAS sus
filas y se comparan en orden (la fila 1 con la fila 1, la 2 con la 2...). Si una clave tiene
distinto numero de filas en A y en B, es diferencia explicita ("FILAS POR CLAVE"). Unica
excepcion: las claves con cursor 0 (intro/carga antes de que la repeticion avance), cuya
cantidad de filas puede variar con la duracion de la intro; ahi se avisa la cantidad y se
compara la ULTIMA fila de cada lado.

Campos comparados por defecto: flow, substate, p1_hash, p2_hash, rng y las palabras crudas
p1_*/p2_*. NO se comparan engine_frame, overall ni flow_hash (llevan el contador de frames del
motor); --estricto los incluye.

Volcado de pelea con guion (autoprueba oracle-training): trae las columnas script_frame
(indice del frame del guion, la clave) e in_p1/in_p2 (la entrada cruda que recibio cada lado). La
clave pasa a ser script_frame (se muestra como "script_frame=N"), no hay intro ni cola: un volcado
es INCOMPLETO si su ultimo script_frame no llega al largo que declara su linea "# script:"
(frames=N). in_p1/in_p2 NO son estado: solo se comparan con --estricto, y se informan APARTE
("primera diferencia de ENTRADA") para que un control negativo (una entrada cambiada) muestre
dos cosas distintas: el frame donde cambio la entrada y el primer frame donde cambia el ESTADO.
--frame-esperado N[-M]: el primer frame de ESTADO distinto debe caer en N (o entre N y M); si no
cae ahi, o no hay diferencia, sale con codigo 6 ("CONTROL NEGATIVO NO CUMPLIDO").

Cola: el volcado termina con 180 filas mas en el ultimo cursor. Si la ultima clave de un
archivo tiene menos de --cola filas (por defecto 180; 0 la desactiva), ese archivo se
considera cortado (INCOMPLETO).
"""
import sys

FLUJO = ("engine_frame", "overall", "flow_hash")
COLA_DEFECTO = 180
TXT_DIAG = "DIAGNOSTICO, no valido como resultado"


def leer(ruta):
    comentarios, cab, filas = [], None, []
    with open(ruta, encoding="utf-8") as f:
        for n, linea in enumerate(f, 1):
            linea = linea.rstrip("\r\n")
            if not linea:
                continue
            if linea.startswith("#"):
                comentarios.append(linea[1:].strip())
            elif cab is None:
                cab = linea.split(",")
                if cab[:4] != ["seq", "round", "cursor", "engine_frame"]:
                    raise ValueError(f"{ruta}:{n}: no parece un volcado del oraculo (cabecera: {linea[:60]})")
            else:
                campos = linea.split(",")
                if len(campos) != len(cab):
                    raise ValueError(f"{ruta}:{n}: {len(campos)} columnas, la cabecera tiene {len(cab)}")
                try:
                    int(campos[1]), int(campos[2])
                except ValueError:
                    raise ValueError(f"{ruta}:{n}: round/cursor no son enteros")
                filas.append(campos)
    if cab is None:
        raise ValueError(f"{ruta}: vacio")
    return comentarios, cab, filas


def por_clave(filas):
    """(round, cursor) -> lista de TODAS sus filas en orden; y el orden de aparicion de las claves."""
    d, orden = {}, []
    for fila in filas:
        k = (int(fila[1]), int(fila[2]))
        if k not in d:
            d[k] = []
            orden.append(k)
        d[k].append(fila)
    return d, orden


def cobertura(nombre, d, orden, total):
    rondas = {}
    for r, c in orden:
        rondas.setdefault(r, []).append(c)
    print(f"  {nombre}: {len(rondas)} rondas, {total} filas, {len(orden)} claves (round,cursor)")
    for r in sorted(rondas):
        cs = rondas[r]
        print(f"    ronda {r}: {len(cs)} cursores (de {min(cs)} a {max(cs)}), {sum(len(d[(r, c)]) for c in cs)} filas")
    if orden:
        k = orden[-1]
        print(f"    ultima clave {k}: {len(d[k])} filas (cola)")


def main(argv):
    args, opciones, cola, esperado = [], set(), COLA_DEFECTO, None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--cola":
            try:
                cola = int(argv[i + 1])
            except (IndexError, ValueError):
                print("ERROR: --cola necesita un entero")
                return 2
            i += 1
        elif a == "--frame-esperado":
            try:
                partes = argv[i + 1].split("-")
                esperado = (int(partes[0]), int(partes[-1]))
                if len(partes) > 2 or esperado[0] > esperado[1]:
                    raise ValueError
            except (IndexError, ValueError):
                print("ERROR: --frame-esperado necesita N o N-M (enteros, N <= M)")
                return 2
            i += 1
        elif a.startswith("--"):
            opciones.add(a)
        else:
            args.append(a)
        i += 1
    desconocidas = opciones - {"--sin-rng", "--estricto", "--todo", "--prefijo"}
    if len(args) != 2 or desconocidas:
        if desconocidas:
            print("ERROR: opciones desconocidas:", ", ".join(sorted(desconocidas)))
        print(__doc__)
        return 2
    sin_rng, estricto, todo, prefijo = ("--sin-rng" in opciones, "--estricto" in opciones,
                                        "--todo" in opciones, "--prefijo" in opciones)
    diag = sin_rng or prefijo
    try:
        ca, ha, fa = leer(args[0])
        cb, hb, fb = leer(args[1])
    except (OSError, ValueError) as e:
        print("ERROR:", e)
        return 2
    if ha != hb:
        # Volcados con columnas distintas (por ejemplo uno con las columnas W0b de cajas y proyectiles y otro sin ellas):
        # se comparan SOLO las columnas comunes, en el orden de A, y se avisa cuales quedaron fuera. Un IGUALES asi no
        # dice nada de las columnas que solo trae uno de los dos (el "campos comparados" del final las nombra).
        comunes = [c for c in ha if c in hb]
        if comunes[:4] != ["seq", "round", "cursor", "engine_frame"] or len(set(ha)) != len(ha) or len(set(hb)) != len(hb):
            print("ERROR: las cabeceras difieren y no comparten el nucleo (seq, round, cursor, engine_frame) o repiten columnas")
            return 2
        solo_a = [c for c in ha if c not in hb]
        solo_b = [c for c in hb if c not in ha]
        print(f"AVISO: cabeceras distintas; se comparan las {len(comunes)} columnas comunes. Solo en A: {len(solo_a)} {solo_a[:4]}{'...' if len(solo_a) > 4 else ''}; solo en B: {len(solo_b)} {solo_b[:4]}{'...' if len(solo_b) > 4 else ''}")
        ia, ib = [ha.index(c) for c in comunes], [hb.index(c) for c in comunes]
        fa = [[f[j] for j in ia] for f in fa]
        fb = [[f[j] for j in ib] for f in fb]
        ha = hb = comunes
    if not fa or not fb:
        print("ERROR: un volcado no tiene filas de datos (A: %d, B: %d)" % (len(fa), len(fb)))
        return 2
    # Pelea con guion: la clave es script_frame (cursor = script_frame + 1, ronda 0).
    guion = "script_frame" in ha
    if guion:
        j_sf = ha.index("script_frame")
        guion = all(int(f[j_sf]) >= 0 for f in fa) and all(int(f[j_sf]) >= 0 for f in fb)
    if esperado and not guion:
        print("ERROR: --frame-esperado solo vale para volcados de pelea con guion (columna script_frame)")
        return 2
    largos = []
    for coms in (ca, cb):
        largo = None
        for c in coms:
            if c.startswith("script:") and "frames=" in c:
                try:
                    largo = int(c.split("frames=")[1].split()[0])
                except ValueError:
                    pass
        largos.append(largo)
    for linea in ("fp:", "replay:"):
        xa = [c for c in ca if c.startswith(linea)]
        xb = [c for c in cb if c.startswith(linea)]
        if xa != xb:
            print(f"AVISO: '{linea}' distinto: A={xa} B={xb}")

    entradas = {"in_p1", "in_p2"}
    ignorar = {"seq", "round", "cursor", "script_frame"} | (set() if estricto else set(FLUJO) | entradas) | ({"rng"} if sin_rng else set())
    cols = [j for j, c in enumerate(ha) if c not in ignorar]
    cols_in = [j for j in cols if ha[j] in entradas]       # solo con --estricto
    cols = [j for j in cols if ha[j] not in entradas]      # el estado
    nombres = [ha[j] for j in cols + cols_in]
    da, oa = por_clave(fa)
    db, ob = por_clave(fb)

    print("COBERTURA")
    cobertura("A", da, oa, len(fa))
    cobertura("B", db, ob, len(fb))

    # Duplicados: cuantas claves tienen mas de una fila, y cuantas filas por clave.
    for nombre, d in (("A", da), ("B", db)):
        multi = [k for k, v in d.items() if len(v) > 1]
        print(f"  {nombre}: {len(multi)} claves con mas de una fila (maximo {max(len(v) for v in d.values())} filas por clave)")

    # Cobertura de claves.
    solo_a = [k for k in oa if k not in db]
    solo_b = [k for k in ob if k not in da]
    corte, cobdist, notas = [], [], []
    ult_cortado = set()  # ultimas claves de archivos cortados: su cantidad de filas no es evidencia
    if solo_a or solo_b:
        # Corte: un lado es prefijo exacto (en orden) del otro.
        if not solo_a and ob[:len(oa)] == oa:
            corte.append(f"A se corta: le faltan {len(solo_b)} claves del final de B (desde {solo_b[0]})")
            ult_cortado.add(oa[-1])
        elif not solo_b and oa[:len(ob)] == ob:
            corte.append(f"B se corta: le faltan {len(solo_a)} claves del final de A (desde {solo_a[0]})")
            ult_cortado.add(ob[-1])
        else:
            cobdist.append(f"claves solo en A: {len(solo_a)} {solo_a[:3]}; solo en B: {len(solo_b)} {solo_b[:3]}")
    if guion:
        for nombre, d, o, largo in (("A", da, oa, largos[0]), ("B", db, ob, largos[1])):
            if largo is None:
                notas.append(f"{nombre}: sin linea '# script: ... frames=N'; no se sabe si el guion llego a su fin")
            elif o[-1][1] < largo:
                ult_cortado.add(o[-1])
                corte.append(f"{nombre}: el guion tiene {largo} frames y el volcado llega al script_frame {o[-1][1] - 1}")
            elif o[-1][1] > largo:
                cobdist.append(f"{nombre}: el volcado pasa del largo del guion ({largo})")
    if cola > 0 and not guion:
        for nombre, d, o in (("A", da, oa), ("B", db, ob)):
            if len(d[o[-1]]) < cola:
                ult_cortado.add(o[-1])
                corte.append(f"{nombre}: la ultima clave {o[-1]} tiene {len(d[o[-1]])} filas, se esperaban >= {cola} (cola final); la ultima ronda no llega a su fin")

    # Comparacion de TODAS las filas de cada clave comun, en orden.
    comparadas, difs, difs_in = 0, [], []  # difs: (clave, tipo, detalle); difs_in: lo mismo, solo columnas de entrada
    for k in oa:
        if k not in db:
            continue
        la, lb = da[k], db[k]
        if k[1] == 0:
            if len(la) != len(lb):
                notas.append(f"intro (cursor 0) {k}: filas A={len(la)} B={len(lb)}; no cuenta, se compara la ultima fila")
            pares = [(la[-1], lb[-1])]
        else:
            pares = list(zip(la, lb))
            if len(la) != len(lb) and k not in ult_cortado:
                difs.append((k, "FILAS POR CLAVE", f"A={len(la)} filas, B={len(lb)} filas"))
        comparadas += len(pares)
        for n, (ra, rb) in enumerate(pares):
            dif = [ha[j] for j in cols if ra[j] != rb[j]]
            if dif:
                difs.append((k, f"fila {n + 1} de la clave" if len(la) > 1 or len(lb) > 1 else "fila", (ra, rb, dif)))
            dif_in = [ha[j] for j in cols_in if ra[j] != rb[j]]
            if dif_in and not any(x[0] == k for x in difs_in):
                difs_in.append((k, "fila", (ra, rb, dif_in)))
            if dif:
                break  # una diferencia por clave basta

    print(f"  filas totales A={len(fa)} B={len(fb)}; claves comunes={len([k for k in oa if k in db])}; filas comparadas={comparadas}")
    print(f"  campos comparados: {', '.join(nombres)}")
    for n in notas[:5]:
        print("AVISO:", n)

    # Informe de diferencias
    if difs:
        k, tipo, det = difs[0]
        etiqueta = f"script_frame={k[1] - 1}" if guion else f"round={k[0]} cursor={k[1]}"
        print(f"PRIMERA DIFERENCIA en {etiqueta} ({tipo})")
        if isinstance(det, tuple):
            ra, rb, dif = det
            print(f"  fila {ra[0]} de A, fila {rb[0]} de B; flow={ra[4]}")
            resumen = [c for c in dif if c in ("overall", "flow_hash", "p1_hash", "p2_hash", "rng", "flow", "substate", "engine_frame")]
            print("  hashes/columnas de flujo distintas:", ", ".join(resumen) or "(ninguna)")
            print("  campos de personaje distintos:", ", ".join(c for c in dif if c not in resumen) or "(ninguno)")
            for c in dif[:12]:
                j = ha.index(c)
                print(f"    {c}: A={ra[j]} B={rb[j]}")
        else:
            print("  ", det)
        print(f"  claves con diferencia en total: {len(difs)}")
        if todo:
            for k2, t2, d2 in difs[1:21]:
                extra = ", ".join(d2[2][:6]) if isinstance(d2, tuple) else d2
                print(f"    {f'script_frame={k2[1] - 1}' if guion else f'round={k2[0]} cursor={k2[1]}'}: {t2}: {extra}")
    if difs_in:
        k, _, (ra, rb, dif) = difs_in[0]
        etiqueta = f"script_frame={k[1] - 1}" if guion else f"round={k[0]} cursor={k[1]}"
        print(f"PRIMERA DIFERENCIA DE ENTRADA en {etiqueta}: " + ", ".join(f"{c} A={ra[ha.index(c)]} B={rb[ha.index(c)]}" for c in dif) +
              f"  ({len(difs_in)} claves con entrada distinta)")
    elif cols_in:
        print("ENTRADAS: in_p1 e in_p2 iguales en todas las filas comparadas")
    for m in corte:
        print("INCOMPLETO:", m)
    for m in cobdist:
        print("COBERTURA DISTINTA:", m)

    # Veredicto
    hay = bool(difs or difs_in)
    if prefijo:
        veredicto = "DIFERENTES" if hay else "IGUALES"
        det = f" (campos: {', '.join(nombres)}; solo prefijo comun)" if not hay else ""
        codigo = 1 if hay else 5
    elif cobdist:
        veredicto, det, codigo = "COBERTURA DISTINTA", "", 4
    elif hay:
        veredicto, det, codigo = "DIFERENTES", "", 1
    elif corte:
        veredicto, det, codigo = "INCOMPLETO", "", 3
    else:
        veredicto, det = "IGUALES", f" (campos: {', '.join(nombres)})"
        codigo = 5 if diag else 0
    marca = ""
    if diag:
        usados = [o for o in ("--sin-rng", "--prefijo") if o in opciones]
        marca = f"  [{TXT_DIAG}: {' '.join(usados)}]"
    print(f"RESULTADO: {veredicto}{det}{marca}")
    if diag and codigo == 5:
        print(f"{TXT_DIAG}: un IGUALES con estas opciones no cuenta; repetir sin ellas.")
    if esperado:
        a, b = esperado
        rango = f"{a}" if a == b else f"{a} a {b}"
        if difs and a <= difs[0][0][1] - 1 <= b:
            print(f"CONTROL NEGATIVO CUMPLIDO: la primera diferencia de ESTADO esta en script_frame={difs[0][0][1] - 1} (esperado {rango})")
        else:
            donde = f"script_frame={difs[0][0][1] - 1}" if difs else "ninguna diferencia de estado"
            print(f"CONTROL NEGATIVO NO CUMPLIDO: {donde}; se esperaba script_frame {rango}")
            return 6
    return codigo


if __name__ == "__main__":
    sys.exit(main(sys.argv))
