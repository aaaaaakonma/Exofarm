import numpy as np, time, sys
from scipy import ndimage as ndi
from skimage.morphology import skeletonize
from skimage.filters import apply_hysteresis_threshold
S = 4
d = np.load('occ_field.npz'); occ = d['occ'].astype(np.float64); hx = d['hx']; hy = d['hy']; zf = d['zf']
H, W = zf.shape; SH, SW = occ.shape

LOW, HIGH, QMIN = float(sys.argv[1]) if len(sys.argv) > 1 else 12, float(sys.argv[2]) if len(sys.argv) > 2 else 48, float(sys.argv[3]) if len(sys.argv) > 3 else 100
SIGMA = float(sys.argv[4]) if len(sys.argv) > 4 else 0.5
PRUNE = int(sys.argv[5]) if len(sys.argv) > 5 else 8

# 1. mask + skeleton
occs = ndi.gaussian_filter(occ, SIGMA) if SIGMA > 0 else occ
mask = apply_hysteresis_threshold(occs, LOW, HIGH)
sk = skeletonize(mask)
K8 = np.ones((3,3), int); K8[1,1] = 0
def nbrs(a): return ndi.convolve(a.astype(int), K8, mode='constant')
def prune(sk0, N):
    cur = sk0.copy()
    for _ in range(N):
        cur &= ~(cur & (nbrs(cur) <= 1))            # peel endpoints
    front = cur & (nbrs(cur) <= 1)                    # regrow surviving branch tips along the original skeleton
    for _ in range(N):
        grow = ndi.binary_dilation(front, structure=np.ones((3,3))) & sk0 & ~cur
        if not grow.any(): break
        cur |= grow; front = grow
    return cur
n0 = int(sk.sum()); sk = prune(sk, PRUNE) if PRUNE > 0 else sk
print(f"skeleton px before/after spur pruning ({PRUNE}): {n0} -> {int(sk.sum())}")
print(f"mask px {mask.sum()}, skeleton px {sk.sum()}")

# 2. heading (unit) at every skeleton px from a 7x7 pooled sum
def box(a, r=3): return ndi.uniform_filter(a.astype(np.float64), 2*r+1, mode='constant') * (2*r+1)**2
HX, HY = box(hx), box(hy); hn = np.hypot(HX, HY); hn[hn == 0] = 1
HX, HY = HX/hn, HY/hn

P = np.argwhere(sk)                    # (i=row, j=col) in sub-grid
n = len(P); pid = -np.ones((SH, SW), np.int64); pid[P[:,0], P[:,1]] = np.arange(n)
hxp, hyp = HX[P[:,0], P[:,1]], HY[P[:,0], P[:,1]]   # flow direction (x=col, y=row)

# 3. Q by cross-section integral, gated to this channel via nearest-skeleton label
dist, (ni, nj) = ndi.distance_transform_edt(~sk, return_indices=True)
nearest = pid[ni, nj]
HW = 10; ts = np.arange(-HW, HW+0.01, 0.5)
si = P[:,0][:,None] + ts[None,:]*hxp[:,None]       # normal = (-hy, hx) in (x,y) => row offset uses +hx, col offset uses -hy
sj = P[:,1][:,None] - ts[None,:]*hyp[:,None]
QSIG = float(sys.argv[7]) if len(sys.argv) > 7 else 1.0
occ_q = ndi.gaussian_filter(occ, QSIG) if QSIG > 0 else occ   # mass-preserving blur so a 1px spike integrates to its true count
vals = ndi.map_coordinates(occ_q, [si.ravel(), sj.ravel()], order=1, mode='nearest').reshape(si.shape)
ri = np.clip(np.rint(si).astype(int), 0, SH-1); rj = np.clip(np.rint(sj).astype(int), 0, SW-1)
lab = nearest[ri, rj]
cen = P[lab]                                            # nearest skeleton px of each sample
ok = np.hypot(cen[...,0] - P[:,0][:,None], cen[...,1] - P[:,1][:,None]) <= 5
Q = (vals*ok).sum(1)*0.5
print("Q (droplets = cells of drainage) range", Q.min().round(1), Q.max().round(1))

offs = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)]
# 4. downstream pointer: best-aligned skeleton neighbour, searching radius 1 then up to RAD to bridge small breaks
RAD = int(sys.argv[6]) if len(sys.argv) > 6 else 3
down = -np.ones(n, np.int64)
for rad in range(1, RAD+1):
    best = np.full(n, 0.3 if rad > 1 else 0.2); todo = down < 0
    cand = -np.ones(n, np.int64)
    for di in range(-rad, rad+1):
        for dj in range(-rad, rad+1):
            if max(abs(di), abs(dj)) != rad: continue
            ii = P[:,0]+di; jj = P[:,1]+dj
            inb = (ii >= 0) & (ii < SH) & (jj >= 0) & (jj < SW)
            q = np.where(inb, pid[np.clip(ii,0,SH-1), np.clip(jj,0,SW-1)], -1)
            dot = (hxp*dj + hyp*di)/np.hypot(di, dj)
            m = (q >= 0) & (dot > best) & todo
            cand[m] = q[m]; best[m] = dot[m]
    down[todo & (cand >= 0)] = cand[todo & (cand >= 0)]

# border distance in cells
bd = np.minimum.reduce([P[:,0]/S, (SH-1-P[:,0])/S, P[:,1]/S, (SW-1-P[:,1])/S])
np.savez('extract_raw.npz', P=P, Q=Q, down=down, hxp=hxp, hyp=hyp, bd=bd)

def report(keep, tag):
    idx = np.where(keep)[0]; remap = -np.ones(n, np.int64); remap[idx] = np.arange(idx.size)
    dn = np.where(down[idx] >= 0, remap[down[idx]], -1)           # downstream within kept set, else -1
    terminal = dn < 0
    # follow to the end (pointer jumping); cycles remain unresolved
    nx = np.where(terminal, np.arange(idx.size), dn)
    for _ in range(60): nx = nx[nx]
    reached_end = terminal[nx]
    endpt = nx
    at_border = bd[idx][endpt] <= 3
    print(f"\n--- {tag}: {idx.size} skeleton px (~{idx.size/S:.0f} cells of channel)")
    print(f" drains to a terminal (no cycles): {reached_end.mean():.4f}")
    print(f" terminals: {int(terminal.sum())}; of these within 3 cells of border: {int((terminal & (bd[idx] <= 3)).sum())}; interior dead-ends: {int((terminal & (bd[idx] > 3)).sum())}")
    print(f" px whose path ends at the border: {(reached_end & at_border).mean():.4f}")
    lk = ~terminal
    r = Q[idx][dn[lk]] / np.maximum(Q[idx][lk], 1e-9)
    print(f" Q monotonicity over links: median ratio down/up {np.median(r):.3f}; ratio<0.9: {(r<0.9).mean():.4f}; <0.7: {(r<0.7).mean():.4f}; <0.5: {(r<0.5).mean():.4f}")
    return idx, dn

keep = Q >= QMIN
idx, dn = report(keep, f"network Q>={QMIN:.0f}")
# junction / spur stats on kept set
kmap = -np.ones((SH, SW), np.int64); kmap[P[idx,0], P[idx,1]] = np.arange(idx.size)
nbc = sum((np.roll(np.roll(kmap>=0, -di, 0), -dj, 1)).astype(int) for di, dj in offs)[P[idx,0], P[idx,1]]
print(f" endpoints (1 nbr): {(nbc==1).sum()}, junction px (>=3 nbr): {(nbc>=3).sum()}")
# loops: Euler characteristic of the kept skeleton graph (8-conn)
lab_cc, ncc = ndi.label(kmap >= 0, structure=np.ones((3,3)))
print(f" connected components: {ncc}")
np.savez('extract_net.npz', idx=idx, dn=dn)
