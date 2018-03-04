import os

batches = [4, 8, 16, 32]
sizes = [512]

for size in sizes:
    for batch in batches:
        os.system("python json_profile.py " + str(size) + " " + str(batch))
