
set terminal pngcairo size 1400,500 enhanced font 'Arial,12'
set output 'bolt-fig12.png'

set multiplot layout 1,2

# ── Shared style ──────────────────────────────────────────────────────────────
set xlabel 'Time ({/Symbol m}s)'
set xrange [0:750]
set yrange [0:9]
set y2range [0:150]
set ytics nomirror
set y2tics
set key top right

# ── (a) Bolt WITH SM ──────────────────────────────────────────────────────────
set title '(a) Bolt'
set ylabel 'Queue Occupancy ({/Symbol m}s)'
set y2label 'Cwnd (KB)'

set label 1 'Flow\nReroute' at 195,7 tc rgb '#ff8800' font ',10'
set arrow 1 from 200,6.5 to 200,0.5 lc rgb '#ff8800' lw 1.5

plot 'bolt-fig12a-queue.dat' u 1:2 w l lc rgb '#2166ac' lw 2 title 'Bolt',  \
     'bolt-fig12a-cwnd.dat'  u 1:2 w l lc rgb '#d6604d' lw 2 axes x1y2 title 'Bolt',  \
     0.1 w l lc rgb '#2166ac' lw 1 dt 2 title 'Ideal Queueing',  \
     100  w l lc rgb '#d6604d' lw 1 dt 2 axes x1y2 title 'Ideal Cwnd'

unset label 1
unset arrow 1

# ── (b) Bolt WITHOUT SM ───────────────────────────────────────────────────────
set title '(b) Bolt (without SM)'
set ylabel ''

set label 2 'Flow\nReroute' at 195,7 tc rgb '#ff8800' font ',10'
set arrow 2 from 200,6.5 to 200,0.5 lc rgb '#ff8800' lw 1.5
set label 3 'Under\nUtilization' at 400,4 tc rgb '#ff8800' font ',10'

plot 'bolt-fig12b-queue.dat' u 1:2 w l lc rgb '#2166ac' lw 2 title 'Bolt (w/o SM)',  \
     'bolt-fig12b-cwnd.dat'  u 1:2 w l lc rgb '#d6604d' lw 2 axes x1y2 title 'Bolt (w/o SM)', \
     0.1 w l lc rgb '#2166ac' lw 1 dt 2 title 'Ideal Queueing',  \
     100  w l lc rgb '#d6604d' lw 1 dt 2 axes x1y2 title 'Ideal Cwnd'

unset label 2
unset label 3
unset arrow 2

unset multiplot
