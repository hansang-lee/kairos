---
description: How to verify the Macro Dashboard and Data locally
---

To verify the dashboard and macro scoring locally, follow these steps:

### 1. Build the project (if C++ logic changed)
Make sure you have built the latest binary:
```zsh
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/Release -j
./build/Release/kairos_tests
```

### 2. Generate updated data.json
Run the macro tool with your FRED API key to get the latest real-world data:
// turbo
```zsh
export FRED_API_KEY=<your_fred_api_key>
./build/Release/app/macro --json config/macro_allocation.json > docs/data.json
```

### 3. Run a local web server (to avoid CORS errors)
The dashboard uses `fetch()` to load `data.json`. This will fail if you open `index.html` directly from the file system. Run a simple server instead:
// turbo
```zsh
cd docs
python3 -m http.server 8766
```
Then visit: [http://localhost:8766](http://localhost:8766)

### 4. Verify UI Refinements
- Check that the **Fear & Greed** bar has the ruler-style ticks and arrow indicator.
- Verify that **Regime Badges** (Expansion, Overheating, etc.) have solid colors.
- Ensure **Payroll** and **CPI** indicators are visible in the breakdown grid.
