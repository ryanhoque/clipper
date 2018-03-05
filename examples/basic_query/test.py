output = ""
with open("data.txt") as fh:
	for line in fh.readlines():
		fields = line.split(' ')
		imgsize = fields[0][0:fields[0].find('x')] # img size
		output += imgsize + " x " + imgsize + "|"
		output += fields[0][fields[0].find('x')+1:] + "|" # batch size
		output += str(round(float(fields[18][:-2]), 2)) + "|" # mean
		output += str(round(float(fields[10][:-1]), 2)) + "|" # std dev
		output += fields[4][:-1] + "|" # min
		output += fields[14][:-1] + "|" # p50
		output += str(round(float(fields[8][:-1]), 2)) + "|" # p95
		output += str(round(float(fields[2][:-1]), 2)) + "|" # p99
		output += fields[6][:-1] + "\n" # max
with open("data2.txt", "w") as fh:
	fh.write(output)
