import os 
import time

loopNums = [1, 10, 20, 30, 40]
eachLoop = 1

for num in loopNums:
    totalTime = 0
    f = open(str(num)+".txt", "w")
    f.write("8192 8192 4 4 "+ str(num))
    f.close()
    for i in range(eachLoop):
        start = time.time()
        os.system("seq-algo.exe <" + str(num) + ".txt")
        end = time.time()
        
        totalTime+= (end-start)
    avTime = totalTime
    print("Average Time of 8192x8192 grid for " + str(num) + " loop: " + str(avTime))