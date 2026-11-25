import numpy as np, heapq, time, sys
import matplotlib.cbook as cb

def load_dem():
    d = cb.get_sample_data('jacksboro_fault_dem.npz')
    return d['elevation'].astype(np.float64)

def priority_flood_eps(z, eps=1e-3):
    """Fill pits so every cell drains to the border; eps ramp on flats. 8-connected."""
    H, W = z.shape
    out = z.copy()
    closed = np.zeros((H, W), bool)
    pq = []
    for r in range(H):
        for c in range(W):
            if r in (0, H-1) or c in (0, W-1):
                heapq.heappush(pq, (out[r, c], r, c)); closed[r, c] = True
    nb = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)]
    while pq:
        h, r, c = heapq.heappop(pq)
        for dr, dc in nb:
            rr, cc = r+dr, c+dc
            if 0 <= rr < H and 0 <= cc < W and not closed[rr, cc]:
                closed[rr, cc] = True
                if out[rr, cc] <= h:
                    out[rr, cc] = np.nextafter(h, np.inf) if eps == 0 else h + eps
                heapq.heappush(pq, (out[rr, cc], rr, cc))
    return out

def bilin(F, x, y):
    H, W = F.shape
    x0 = np.clip(np.floor(x).astype(np.int64), 0, W-2)
    y0 = np.clip(np.floor(y).astype(np.int64), 0, H-2)
    fx = np.clip(x - x0, 0, 1); fy = np.clip(y - y0, 0, 1)
    return (F[y0, x0]*(1-fx)*(1-fy) + F[y0, x0+1]*fx*(1-fy)
            + F[y0+1, x0]*(1-fx)*fy + F[y0+1, x0+1]*fx*fy)

def run(zf, inertia=0.5, step=0.25, S=4, maxsteps=12000, seed=0, log=print):
    H, W = zf.shape
    gy, gx = np.gradient(zf)
    rng = np.random.default_rng(seed)
    rr, cc = np.mgrid[0:H, 0:W]
    px = (cc + rng.uniform(-0.5, 0.5, (H, W))).ravel()   # one jittered droplet per cell
    py = (rr + rng.uniform(-0.5, 0.5, (H, W))).ravel()
    px = np.clip(px, 0, W-1); py = np.clip(py, 0, H-1)
    N = px.size
    dx = np.zeros(N); dy = np.zeros(N)
    ids = np.arange(N)
    SH, SW = H*S, W*S
    occ = np.zeros(SH*SW, np.int32)
    def cell(x, y):
        i = np.clip(((y+0.5)*S).astype(np.int64), 0, SH-1)
        j = np.clip(((x+0.5)*S).astype(np.int64), 0, SW-1)
        return i*SW + j
    last = cell(px, py)
    pend = [last.copy()]
    outcome = np.zeros(N, np.int8)  # 0 running, 1 outlet, 2 cap, 3 stall
    nsteps = np.zeros(N, np.int32)
    t0 = time.time()
    for t in range(maxsteps):
        if ids.size == 0: break
        g_x = bilin(gx, px, py); g_y = bilin(gy, px, py)
        gm = np.hypot(g_x, g_y)
        stall = gm < 1e-12
        gm[stall] = 1
        dx = inertia*dx - (1-inertia)*g_x/gm
        dy = inertia*dy - (1-inertia)*g_y/gm
        dm = np.hypot(dx, dy); dm[dm == 0] = 1
        dx /= dm; dy /= dm
        px = px + step*dx; py = py + step*dy
        out = (px < 0) | (px > W-1) | (py < 0) | (py > H-1)
        c = cell(px, py)
        ch = (c != last) & ~out
        if ch.any(): pend.append(c[ch])
        last = c
        done = out | stall
        if done.any():
            outcome[ids[out]] = 1; outcome[ids[stall & ~out]] = 3
            nsteps[ids[done]] = t+1
        if (t+1) % 200 == 0:
            occ += np.bincount(np.concatenate(pend), minlength=SH*SW).astype(np.int32); pend = [last[:0]]
            log(f"step {t+1} active {ids.size - int(done.sum())} {time.time()-t0:.0f}s")
        keep = ~done
        if not keep.all():
            ids, px, py, dx, dy, last = ids[keep], px[keep], py[keep], dx[keep], dy[keep], last[keep]
    occ += np.bincount(np.concatenate(pend), minlength=SH*SW).astype(np.int32)
    outcome[ids] = 2; nsteps[ids] = maxsteps
    return occ.reshape(SH, SW), outcome, nsteps

if __name__ == '__main__':
    z = load_dem()
    t = time.time(); zf = priority_flood_eps(z); print('fill', time.time()-t, 'cells raised', int((zf > z+1e-9).sum()))
    occ, outc, ns = run(zf, log=lambda s: print(s, flush=True))
    print('outcomes outlet/cap/stall:', [(outc == k).mean().round(4) for k in (1, 2, 3)])
    np.savez_compressed('occ.npz', occ=occ, zf=zf, z=z, outcome=outc, nsteps=ns)
    print('saved')
