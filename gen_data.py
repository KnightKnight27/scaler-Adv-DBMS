#!/usr/bin/env python3
"""Generate users.csv and orders.csv for the DB comparison experiment.

Usage: python3 gen_data.py [N_USERS] [N_ORDERS]
Defaults: 100000 users, 1000000 orders (matches the reference report).
"""
import csv
import random
import sys
from datetime import date, timedelta

N_USERS = int(sys.argv[1]) if len(sys.argv) > 1 else 100_000
N_ORDERS = int(sys.argv[2]) if len(sys.argv) > 2 else 1_000_000

COUNTRIES = ["US", "IN", "DE", "FR", "BR", "JP", "GB", "CA", "AU", "MX"]
STATUSES = ["pending", "paid", "shipped", "cancelled", "refunded"]
START = date(2023, 1, 1)
SPAN_DAYS = 900

random.seed(42)

with open("users.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["user_id", "country", "signup_date", "is_premium"])
    for uid in range(1, N_USERS + 1):
        country = random.choice(COUNTRIES)
        signup = START + timedelta(days=random.randint(0, SPAN_DAYS))
        is_premium = 1 if random.random() < 0.2 else 0
        w.writerow([uid, country, signup.isoformat(), is_premium])

with open("orders.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["order_id", "user_id", "order_date", "amount", "status"])
    for oid in range(1, N_ORDERS + 1):
        uid = random.randint(1, N_USERS)
        odate = START + timedelta(days=random.randint(0, SPAN_DAYS))
        amount = round(random.uniform(5, 500), 2)
        status = random.choice(STATUSES)
        w.writerow([oid, uid, odate.isoformat(), amount, status])

print(f"Wrote users.csv ({N_USERS} rows) and orders.csv ({N_ORDERS} rows)")
