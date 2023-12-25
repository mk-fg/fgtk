## Library of gawk helpers for human-readable number formatting
## Uses SI suffixes (powers of 10, e.g. 1_000 = 1K), NOT binary 2^10 ones
## Example usage: gawk -i /path/to/numfmt.awk '{printf("%-20s %s\n", $1, numfmt($2, 2))}' /proc/meminfo

# suff_level=1 - force-add suffix, =2 to start it from M, =3 for G, etc
function numfmt(s, suff_level) {
	m = suff_level ? suff_level - 1 : 0
	while (match(s,/^[0-9]+[0-9]{3}$/) && m < 5) {
		k = length(s) - 3
		if ((suff_level && k >= 1) || substr(s, k+1)=="000") { s = substr(s, 1, k); m++ }
		else while (match(s, /[0-9]{4}(_|$)/))
			s = substr(s, 1, RSTART) "_" substr(s, RSTART+1) }
	return s substr("KMGTP", m, m ? 1 : 0) }

# suff_level=1 - strip last 000, =2 - 6*0, etc
function numfmt_parse(s, suff_level) {
	if (match(s,/^([0-9]+_)*[0-9]+[KMGTP]?$/)) {
		sub("_", "", s)
		if (m=index("KMGTP", substr(s, length(s), 1))) {
			m = m - suff_level
			s=substr(s, 1, length(s) - 1); while (m-- > 0) s = s "000" } }
	return s }
