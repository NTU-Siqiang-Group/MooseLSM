import requests
import random
import string

url = "http://127.0.0.1:1080/KvServerCtrl"
randlist = string.ascii_uppercase + string.digits + string.ascii_lowercase

def range_read(key):
  data = f"key={key}&length=10"
  r = requests.get(f'{url}/range?{data}')
  print(r.text)

def put(key, value):
  data = f'key={key}&value={value}'
  r = requests.put(f'{url}/put?{data}')
  print(r.text)


def get(key):
  data = f'key={key}'
  r = requests.get(f"{url}/get?{data}")
  print(r.text)


def rand_key(len):
  ret = ''
  for _ in range(len):
    ret += random.choice(randlist)
  return ret

def random_test(num):
  for _ in range(num):
    key = rand_key(512)
    put(key, key)
    range_read(key)
    get(key)
    if random.random() < 0.5:
      get(rand_key(512))

random_test(1000000)