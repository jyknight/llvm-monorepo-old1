#!/usr/bin/gnuplot -p

set border 3
set xtics axis nomirror
set ytics axis nomirror
set xlabel "Time"
set ylabel "Memory usage"
set xdata time
set timefmt "%s"
set format x "%M:%S"
set format y "%.1s %cB"
set datafile separator ","

set term wxt 0
plot \
	"j3-stale-ref.csv"	\
	using 2:3	\
	title "Presence of stale references"	\
	with lines linewidth "2pt" linecolor rgb "red",	\
	"j3-stale-ref-corrected.csv"	\
	using 2:3	\
	title "Stale references corrected"	\
	with lines linewidth "3pt" linetype "dotted" linecolor rgb "blue"	\

#set term wxt 1
#plot \
#	"j3-stale-ref.csv"	\
#	using 2:3	\
#	title "Presence of stale references"	\
#	with lines linewidth "2pt" linecolor rgb "red",	\
#	"j3-stale-ref-corrected.csv"	\
#	using 2:3	\
#	title "Stale references corrected"	\
#	with lines linewidth "3pt" linetype "dotted" linecolor rgb "blue"	\
