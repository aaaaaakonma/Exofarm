import numpy as np, time
import sim
from diag4 import mfd_field

def run_field(zf, p=2.0, step=0.25, S=4, maxsteps=20000, seed=0, log=print):
    H, W = zf.shape
    vx, vy = mfd_field(zf, p)
    rng = np.random.default_rng(seed)
    rr, cc = np.mgrid[0:H, 0:W]
    px = np.clip((cc + rng.uniform(-0.5, 0.5, (H, W))).ravel(), 0, W-1)
    py = np.clip((rr + rng.uniform(-0.5, 0.5, (H, W))).ravel(), 0, H-1)
    N = px.size; ids = np.arange(N)
    SH, SW = H*S, W*S
    occ = np.zeros(SH*SW, np.int32); hxs = np.zeros(SH*SW, np.float32); hys = np.zeros(SH*SW, np.float32)
    hp = []
    exit_xy = np.full((N, 2), np.nan, np.float32)
    def cell(x, y):
        return np.clip(((y+0.5)*S).astype(np.int64), 0, SH-1)*SW + np.clip(((x+0.5)*S).astype(np.int64), 0, SW-1)
    def at_edge(x, y):
        r = np.rint(y); c = np.rint(x)
        return (r <= 0) | (r >= H-1) | (c <= 0) | (c >= W-1)
    # droplets that spawn in an edge cell exit immediately
    e0 = at_edge(px, py)
    exit_xy[ids[e0]] = np.c_[px[e0], py[e0]]
    keep = ~e0; ids, px, py = ids[keep], px[keep], py[keep]
    last = cell(px, py); pend = [last.copy()]
    outcome = np.zeros(N, np.int8); outcome[e0] = 1
    t0 = time.time()
    for t in range(maxsteps):
        if ids.size == 0: break
        a = sim.bilin(vx, px, py); b = sim.bilin(vy, px, py)
        m = np.hypot(a, b); stall = m < 1e-9; m[stall] = np.inf
        px = px + step*a/m; py = py + step*b/m
        out = at_edge(px, py)
        c = cell(px, py); ch = (c != last)
        if ch.any(): pend.append(c[ch]); hp.append(np.c_[a[ch]/m[ch], b[ch]/m[ch]].astype(np.float32))
        last = c
        done = out | stall
        if done.any():
            j = ids[out]; exit_xy[j] = np.c_[px[out], py[out]]; outcome[j] = 1
            outcome[ids[stall & ~out]] = 3
        if (t+1) % 400 == 0:
            idxs = np.concatenate(pend); occ += np.bincount(idxs, minlength=SH*SW).astype(np.int32); H_ = np.concatenate(hp) if hp else np.zeros((0,2),np.float32); hxs += np.bincount(idxs[len(pend[0]):], weights=H_[:,0], minlength=SH*SW).astype(np.float32); hys += np.bincount(idxs[len(pend[0]):], weights=H_[:,1], minlength=SH*SW).astype(np.float32); pend = [last[:0]]; hp = []
            log(f"step {t+1} active {ids.size - int(done.sum())} {time.time()-t0:.0f}s")
        if done.any():
            k = ~done; ids, px, py, last = ids[k], px[k], py[k], last[k]
    idxs = np.concatenate(pend); occ += np.bincount(idxs, minlength=SH*SW).astype(np.int32)
    if hp:
        H_ = np.concatenate(hp); hxs += np.bincount(idxs[len(pend[0]):], weights=H_[:,0], minlength=SH*SW).astype(np.float32); hys += np.bincount(idxs[len(pend[0]):], weights=H_[:,1], minlength=SH*SW).astype(np.float32)
    outcome[ids] = 2
    return occ.reshape(SH, SW), outcome, exit_xy, hxs.reshape(SH, SW), hys.reshape(SH, SW)

if __name__ == '__main__':
    from ref import zf, z
    occ, outc, ex, hxs, hys = run_field(zf, log=lambda s: print(s, flush=True))
    print('outcomes outlet/cap/stall:', [round(float((outc == k).mean()), 4) for k in (1, 2, 3)])
    np.savez_compressed('occ_field.npz', hx=hxs, hy=hys, occ=occ, zf=zf, z=z, outcome=outc, exit_xy=ex)
    print('saved')
