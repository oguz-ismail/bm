{
	for (i = 1; i < 5; i++)
		cell[NR, i] = $i

	sub(/ *[^ ]+ +[^ ]+ +[^ ]+ +[^ ]+ +/, "")
	cell[NR, 5] = $0
}

END {
	unit = cell[2, 2]

	if (pat != "")
		for (i = 2; i < NR; i++)
			if (cell[i, 1] == pat || cell[i, 5] ~ pat) {
				unit = cell[i, 2]
				break
			}

	for (i = 2; i < NR; i++)
		for (j = 2; j < 5; j++)
			cell[i, j] = sprintf("%.3f", cell[i, j]/unit)

	split("%3s, %13s, %13s, %13s,  %s\n", fmt, ",")
	for (i = 1; i < NR; i++) {
		for (j = 1; j <= 5; j++)
			printf fmt[j], cell[i, j]
	}
}
