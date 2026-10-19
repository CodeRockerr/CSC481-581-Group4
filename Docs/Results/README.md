# Part 1C results: binary vs text wire format

Generated with:

```
Tools/net_experiments.sh build/Release Docs/Results/net_results.csv 5000 5
python3 Tools/net_analyze.py Docs/Results/net_results.csv
```

**Environment of this run:** MacBook Air, Apple Silicon (arm64), macOS; server and all clients on the
same machine over loopback TCP, Release build, 5000 measured iterations after 200 warm-up
iterations, 5 repetitions per case, fresh server per run. Variance is the sample variance (n - 1)
of per-run elapsed time.

Files: `net_results.csv` (raw, one row per client per run), `summary.csv` / `summary.md`
(mean and variance per case), `figure1_E1.png`, `figure2_E2.png`, `figure3_E3.png`.
