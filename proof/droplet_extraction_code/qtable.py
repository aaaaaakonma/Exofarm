import numpy as np
r = np.load('extract_raw.npz'); P, Q, down, bd = r['P'], r['Q'], r['down'], r['bd']
nt = np.load('extract_net.npz'); idx, dn = nt['idx'], nt['dn']
Qi = Q[idx]; lk = dn >= 0
ratio = np.full(idx.size, np.nan); ratio[lk] = Qi[dn[lk]] / Qi[lk]
print(" Q bin           links   median   p10     frac<0.9   frac<0.7  frac>1.3")
for lo, hi in [(100,300),(300,1000),(1000,3000),(3000,10000),(10000,1e9)]:
    m = lk & (Qi >= lo) & (Qi < hi); x = ratio[m]
    print(f" [{lo:>6},{hi:>9.0f}) {m.sum():6d}  {np.median(x):.3f}  {np.percentile(x,10):.3f}   {(x<0.9).mean():.3f}     {(x<0.7).mean():.3f}    {(x>1.3).mean():.3f}")
