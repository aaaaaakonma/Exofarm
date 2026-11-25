import numpy as np, time
import sim
from ref import *

def mfd_field(zf, p=1.0):
    H, W = zf.shape
    vx = np.zeros((H,W)); vy = np.zeros((H,W)); wsum = np.zeros((H,W))
    for dr, dc in nb:
        sh = np.full((H,W), np.inf)
        r0,r1 = max(0,-dr), H-max(0,dr); c0,c1 = max(0,-dc), W-max(0,dc)
        sh[r0:r1, c0:c1] = zf[r0+dr:r1+dr, c0+dc:c1+dc]
        s = np.maximum((zf - sh)/np.hypot(dr,dc), 0)**p
        n = np.hypot(dr,dc); vx += s*dc/n; vy += s*dr/n; wsum += s
    wsum[wsum == 0] = 1
    return vx/wsum, vy/wsum

def trial_field(vx, vy, step=0.25, n=4000, maxsteps=20000, seed=3):
    rng = np.random.default_rng(seed)
    sx = rng.uniform(0, W-1, n); sy = rng.uniform(0, H-1, n)
    px, py = sx.copy(), sy.copy()
    alive = np.ones(n, bool); ex = np.full(n, np.nan); ey = ex.copy(); T = np.zeros(n)
    for t in range(maxsteps):
        i = np.where(alive)[0]
        if i.size == 0: break
        a = sim.bilin(vx, px[i], py[i]); b = sim.bilin(vy, px[i], py[i])
        m = np.hypot(a, b); m[m < 1e-9] = np.inf
        px[i] += step*a/m; py[i] += step*b/m
        o = (px[i] < 0) | (px[i] > W-1) | (py[i] < 0) | (py[i] > H-1)
        j = i[o]; ex[j] = np.clip(px[j],0,W-1); ey[j] = np.clip(py[j],0,H-1); alive[j] = False; T[j] = t
    ok = ~alive
    cr = np.clip(np.rint(sy).astype(int),0,H-1); cc = np.clip(np.rint(sx).astype(int),0,W-1)
    o8 = d8_out[cr, cc]; o8r, o8c = o8//W, o8 % W
    d = np.abs(perim(ey[ok], ex[ok]) - perim(o8r[ok], o8c[ok])); d = np.minimum(d, P-d)
    return ok.mean(), np.median(d), np.percentile(d,75), np.percentile(d,90), (d<=3).mean(), np.median(T[ok])*step

if __name__ == '__main__':
    print("variant            reached  median  p75   p90  within3  median_path_len")
    for p in (1.0, 2.0):
        vx, vy = mfd_field(zf, p)
        t0 = time.time(); r = trial_field(vx, vy)
        print(f"MFD-field p={p}     {r[0]:.3f}   {r[1]:5.1f} {r[2]:5.1f} {r[3]:5.1f}  {r[4]:.3f}   {r[5]:.0f}   ({time.time()-t0:.0f}s)", flush=True)
