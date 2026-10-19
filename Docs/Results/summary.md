Elapsed time for 5000 loop iterations: mean and sample variance over runs.

## E1: Moving objects in scene (1 client)

| x | format | runs | mean (ms) | variance (ms²) | std dev (ms) | bytes/snapshot |
|---|---|---|---|---|---|---|
| 10 | binary | 5 | 362.7 | 2.4 | 1.6 | 418 |
| 100 | binary | 5 | 402.6 | 1.7 | 1.3 | 3658 |
| 500 | binary | 5 | 609.1 | 22.4 | 4.7 | 18058 |
| 10 | text | 5 | 420.6 | 9.1 | 3.0 | 602 |
| 100 | text | 5 | 863.9 | 18.5 | 4.3 | 5547 |
| 500 | text | 5 | 3173.0 | 152.4 | 12.3 | 28062 |

## E2: Connected clients (100 moving objects)

| x | format | runs | mean (ms) | variance (ms²) | std dev (ms) | bytes/snapshot |
|---|---|---|---|---|---|---|
| 1 | binary | 5 | 405.5 | 96.3 | 9.8 | 3658 |
| 2 | binary | 5 | 496.8 | 20.8 | 4.6 | 3694 |
| 4 | binary | 5 | 648.1 | 1.7 | 1.3 | 3766 |
| 1 | text | 5 | 871.3 | 310.3 | 17.6 | 5547 |
| 2 | text | 5 | 955.9 | 8.2 | 2.9 | 5601 |
| 4 | text | 5 | 1163.5 | 1946.2 | 44.1 | 5709 |

## E3: Objects owned by each client (2 clients)

| x | format | runs | mean (ms) | variance (ms²) | std dev (ms) | bytes/snapshot |
|---|---|---|---|---|---|---|
| 1 | binary | 5 | 478.6 | 1249.6 | 35.3 | 454 |
| 100 | binary | 5 | 549.5 | 2.4 | 1.6 | 7582 |
| 500 | binary | 5 | 931.1 | 9.0 | 3.0 | 36381 |
| 1 | text | 5 | 528.9 | 119.8 | 10.9 | 654 |
| 100 | text | 5 | 1790.4 | 236.5 | 15.4 | 11038 |
| 500 | text | 5 | 6962.2 | 6725.9 | 82.0 | 53839 |
