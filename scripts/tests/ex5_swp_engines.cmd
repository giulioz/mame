// Exercise the TG ROM's VL, AN and AN+FDSP SWP30 loader paths.
// Run together with scripts/tests/ex5_swp_trace.lua.  This deliberately
// redirects the sub CPU after normal boot and is a core regression, not an
// end-user UI path.
focus subcpu
gtime #7000
do w@a6f000=e0ff

// VL: complete program at step 0, control 43 = 0003.
do w@58109e=0003
do r4=003c8d68
do r15=00a70000
do rp=00a6f000
do pc=00224c00
g a6f000
gtime #100

// AN: second half at c0, then common half at 0.  The second-half loader
// writes transitional 0100; the completed engine uses 0101.
do r4=002964f0
do r5=00000001
do r15=00a70000
do rp=00a6f000
do pc=0030fcc4
g a6f000
do w@58109e=0101
do r4=0028ee54
do r5=00000000
do r15=00a70000
do rp=00a6f000
do pc=00301892
g a6f000
gtime #100

// AN+FDSP: replace the common half while retaining the second half.
do w@58109e=0001
do r4=0028ee54
do r5=00000000
do r15=00a70000
do rp=00a6f000
do pc=00301892
g a6f000
gtime #100
quit
