import aiohttp
import asyncio
import time

PROXY = "http://127.0.0.1:8080"
HOST = "http://ccfit.nsu.ru/~rzheutskiy/test_files/"

SMALL = "50mb.dat"
MEDIUM = "100mb.dat"


async def do_request(additional_path, sleep_time):
    await asyncio.sleep(sleep_time)
    path = f"{HOST}{additional_path}"
    print(f"Do request to {path}.")
    ts = time.time()
    async with aiohttp.ClientSession() as session:
        async with session.get(path, proxy=PROXY) as resp:
            print(f"Read size: {len(await resp.read())}")
            print("Status:", resp.status)
    te = time.time()
    print(f"Time: {te - ts} sec.")
    print()


async def main():

    loop = asyncio.get_event_loop()
    tasks = []
    for i in range(50):
        tasks.append(do_request("50mb.dat", 0))
    for i in range(50):
        tasks.append(do_request("100mb.dat", 0))
    await asyncio.gather(*tasks)



if __name__ == '__main__':
    asyncio.run(main())
