import numpy as np, time
import sim
z = sim.load_dem(); zf = sim.priority_flood_eps(z)
H, W = zf.shape
gy, gx = np.gradient(zf)

# D8 pointers on filled DEM
nb = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)]
best = np.full((H,W), -1); bs = np.zeros((H,W))
for k,(dr,dc) in enumerate(nb):
    sh = np.full((H,W), np.inf)
    r0,r1 = max(0,-dr), H-max(0,dr); c0,c1 = max(0,-dc), W-max(0,dc)
    sh[r0:r1, c0:c1] = zf[r0+dr:r1+dr, c0+dc:c1+dc]
    s = (zf - sh)/np.hypot(dr,dc)
    m = s > bs; bs[m] = s[m]; best[m] = k
idx = np.arange(H*W).reshape(H,W)
dr_ = np.array([n[0] for n in nb]+[0]); dc_ = np.array([n[1] for n in nb]+[0])
tr = np.clip(np.arange(H)[:,None] + dr_[best], 0, H-1); tc = np.clip(np.arange(W)[None,:] + dc_[best], 0, W-1)
edge = np.zeros((H,W), bool); edge[0]=edge[-1]=True; edge[:,0]=edge[:,-1]=True
nxt = np.where(edge | (best<0), idx, tr*W+tc).ravel()
for _ in range(20): nxt = nxt[nxt]          # pointer jumping
d8_out = nxt.reshape(H,W)
def perim(r, c):                              # position along the border, clockwise from (0,0)
    r = np.asarray(r, float); c = np.asarray(c, float)
    p = np.where(r <= 0, c, np.where(c >= W-1, (W-1)+r, np.where(r >= H-1, (W-1)+(H-1)+((W-1)-c), 2*(W-1)+(H-1)+((H-1)-r))))
    return p
P = 2*(W-1)+2*(H-1)
def trial(inertia, step, n=4000, maxsteps=8000, seed=3):
    rng = np.random.default_rng(seed)
    sx = rng.uniform(0, W-1, n); sy = rng.uniform(0, H-1, n)
    px, py = sx.copy(), sy.copy()
    dx = np.zeros(n); dy = np.zeros(n); alive = np.ones(n, bool); ex = np.full(n, np.nan); ey = ex.copy()
    for t in range(maxsteps):
        i = np.where(alive)[0]
        if i.size == 0: break
        a = sim.bilin(gx, px[i], py[i]); b = sim.bilin(gy, px[i], py[i])
        gm = np.hypot(a, b); gm[gm == 0] = 1
        dx[i] = inertia*dx[i] - (1-inertia)*a/gm; dy[i] = inertia*dy[i] - (1-inertia)*b/gm
        dm = np.hypot(dx[i], dy[i]); dm[dm == 0] = 1; dx[i] /= dm; dy[i] /= dm
        px[i] += step*dx[i]; py[i] += step*dy[i]
        o = (px[i] < 0) | (px[i] > W-1) | (py[i] < 0) | (py[i] > H-1)
        j = i[o]; ex[j] = np.clip(px[j],0,W-1); ey[j] = np.clip(py[j],0,H-1); alive[j] = False
    ok = ~alive
    cell_r = np.clip(np.rint(sy).astype(int),0,H-1); cell_c = np.clip(np.rint(sx).astype(int),0,W-1)
    o8 = d8_out[cell_r, cell_c]; o8r, o8c = o8//W, o8 % W
    d = np.abs(perim(ey[ok], ex[ok]) - perim(o8r[ok], o8c[ok])); d = np.minimum(d, P-d)
    return ok.mean(), np.median(d), np.percentile(d, 75), np.percentile(d, 90), (d <= 3).mean()
