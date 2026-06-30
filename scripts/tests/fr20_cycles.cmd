// Run against ex5r with -debug -debugger none -debugscript <this file>.
focus maincpu
do pc=700000
do ps=000f0000
do w@700000=9fa0
s 1
logerror "CYCLE nop=%d\n",lastinstructioncycles
do pc=700000
do r0=700100
do b@700100=ff
do w@700000=8050
s 1
logerror "CYCLE band=%d\n",lastinstructioncycles
do pc=700000
do r0=700100
do b@700100=ff
do w@700000=8850
s 1
logerror "CYCLE btst=%d\n",lastinstructioncycles
do pc=700000
do r0=1
do r1=2
do w@700000=ab10
s 1
logerror "CYCLE mulu=%d\n",lastinstructioncycles
do pc=700000
do w@700000=e001
s 1
logerror "CYCLE bra=%d\n",lastinstructioncycles
do pc=700000
do w@700000=e101
s 1
logerror "CYCLE bno=%d\n",lastinstructioncycles
quit
