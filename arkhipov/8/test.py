import subprocess
import math

def run_once(th_cnt : int) -> str:
    res = subprocess.run(["./main", str(th_cnt)], capture_output=True)
    if res.returncode != 0:
        return None
    return res.stdout.decode().split('\n')[0].split(' ')[-1]

def main() -> None:
    
    print("======================")
    results = set()
    for i in range(1, 32749, 1000):
        print(f"Run {i} threads:")
        result = run_once(i)
        print(f'result: {result}')
        results.add(result)
        print()
    status = "OK" if len(results) == 1 else "ERROR"
    print( f"unique values: {','.join(list(results))} {status}")
    
    if status == "ERROR":
        return
    print("======================")

    res = results.pop()
    print(f"Result:  {res}")
    print(f"Math PI: {math.pi}")
    prec = math.log(math.pi - float(res), 10) + 1
    if prec > 0:
        print("Incorrect")
        return
    prec_int = int(prec * -1)
    print(f"Precision: {prec_int} OK")

    print("======================")
    

if __name__ == '__main__':
    main()