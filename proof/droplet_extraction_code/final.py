import numpy as np, matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from scipy import ndimage as ndi
from ref import zf, z, nb, H, W
S = 4
r = np.load('extract_raw.npz'); P, Q, down, bd = r['P'], r['Q'], r['down'], r['bd']
nt = np.load('extract_net.npz'); idx, dn = nt['idx'], nt['dn']
occ = np.load('occ_field.npz')['occ']

# ---- independent references: MFD (Freeman p=1.1) and D8 accumulation on the same filled DEM
order = np.argsort(-zf.ravel(), kind='stable')
def accumulate(mode):
    A = np.ones((H, W)); Af = A.ravel(); zr = zf.ravel()
    for f in order:
        rr, cc = divmod(f, W); ws = []; ts = []
        for dr, dc in nb:
            r2, c2 = rr+dr, cc+dc
            if 0 <= r2 < H and 0 <= c2 < W and zf[r2, c2] < zf[rr, cc]:
                ws.append(((zf[rr, cc]-zf[r2, c2])/np.hypot(dr, dc))); ts.append(r2*W+c2)
        if not ws: continue
        ws = np.array(ws)
        if mode == 'd8': w = (ws == ws.max()).astype(float); w /= w.sum()
        else: w = ws**1.1; w /= w.sum()
        for t, wi in zip(ts, w): Af[t] += Af[f]*wi
    return Af.reshape(H, W), 
mfd, = accumulate('mfd'); d8, = accumulate('d8')
np.savez('refs.npz', mfd=mfd, d8=d8)

# ---- accuracy of Q on the skeleton
ci = np.clip(np.rint((P[idx,0]+0.5)/S-0.5).astype(int), 0, H-1); cj = np.clip(np.rint((P[idx,1]+0.5)/S-0.5).astype(int), 0, W-1)
def max3(A): return ndi.maximum_filter(A, 3)[ci, cj]
Qi = Q[idx]; rm = Qi/max3(mfd); rd = Qi/max3(d8)
rawmax = ndi.maximum_filter(occ.astype(float), 3)[P[idx,0], P[idx,1]]
print("Q bin          n    Q/MFD(max3x3) median [p10,p90]    Q/D8 median [p10,p90]    Q/rawcount(max3x3) median [p10,p90]")
for lo, hi in [(100,300),(300,1000),(1000,3000),(3000,10000),(10000,1e9)]:
    m = (Qi>=lo)&(Qi<hi)
    f = lambda x: f"{np.median(x[m]):.2f} [{np.percentile(x[m],10):.2f},{np.percentile(x[m],90):.2f}]"
    print(f"[{lo:>6},{hi:>9.0f}) {m.sum():6d}   {f(rm):24s}   {f(rd):24s}   {f(Qi/np.maximum(rawmax,1))}")

# ---- render: full network width ~ sqrt(Q) over hillshade
def xy(k):  # sub-grid -> cell coords
    return (P[k,1]+0.5)/S-0.5, (P[k,0]+0.5)/S-0.5
lk = np.where(dn >= 0)[0]
a = idx[lk]; b = idx[dn[lk]]
ax_, ay_ = xy(a); bx_, by_ = xy(b)
segs = np.stack([np.c_[ax_, ay_], np.c_[bx_, by_]], 1)
lw = np.clip(0.25*np.sqrt(Q[a]/100.0), 0.3, 3.2)
ls = matplotlib.colors.LightSource(315, 45); hs = ls.hillshade(zf, vert_exag=2)
fig, ax = plt.subplots(figsize=(13, 11))
ax.imshow(hs, cmap='gray', vmin=0.2, vmax=1.1, alpha=0.8)
ax.add_collection(LineCollection(segs, linewidths=lw, colors='#1f6fd0', capstyle='round')); ax.set_xlim(0, W-1); ax.set_ylim(H-1, 0); ax.axis('off')
ax.set_title('droplet-extracted centerlines, line width ∝ √Q   (Q ≥ 100 cells)')
plt.tight_layout(); plt.savefig('render_full.png', dpi=75); plt.close()

# ---- crop comparison: D8 stream cells vs droplet centerlines, same Q threshold
y0, y1, x0, x1 = 100, 240, 160, 330
fig, ax = plt.subplots(1, 2, figsize=(17, 7.6))
for a_ in ax: a_.imshow(hs, cmap='gray', vmin=0.2, vmax=1.1, alpha=0.8); a_.set_xlim(x0, x1); a_.set_ylim(y1, y0); a_.axis('off')
rr_, cc_ = np.where(d8 >= 100)
ax[0].scatter(cc_, rr_, s=np.clip(np.sqrt(d8[rr_, cc_]/100)*2.2, 1, 22), c='#d0481f', marker='s', linewidths=0)
ax[0].set_title('D8 accumulation ≥ 100 cells (cell-locked)')
ax[1].add_collection(LineCollection(segs, linewidths=np.clip(0.25*np.sqrt(Q[a]/100.0)*1.6, 0.5, 5), colors='#1f6fd0', capstyle='round'))
ax[1].set_title('droplet centerlines, Q ≥ 100 (sub-cell, continuous)')
plt.tight_layout(); plt.savefig('render_crop.png', dpi=70); plt.close()
print("saved")
