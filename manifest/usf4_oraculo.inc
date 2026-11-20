# Sustituciones del oraculo nativo (src/oraculo.cpp). El lifter emite el cuerpo
# de cada una como f_<VA>_lifted y el proyecto define f_<VA>. Solo actuan con
# USF4_ORACLE_DUMP; sin esa variable llaman directo al cuerpo liftado.
replace 5d6b10
replace 5171b0
replace 517130
replace 538110
replace 6ab3b0
replace 614cb0
replace 780380
