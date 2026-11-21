# Wandelt Natural-Earth-GeoJSON (gemeinfrei, naturalearthdata.com) in kompakte Binärdateien für den Globus:
#   uint32 Anzahl Linien, je Linie: uint16 Punkte, dann Punkte als int16 (Länge*100, Breite*100), little endian
# Aufruf: python -I make_world.py <ne_110m_land.geojson> <ne_110m_admin_0_boundary_lines_land.geojson> <Zielordner>
import json, struct, sys, os

def lines(path, polygons):
    out = []
    for f in json.load(open(path, encoding='utf-8'))['features']:
        g = f['geometry']
        t, c = g['type'], g['coordinates']
        if t == 'Polygon': parts = c
        elif t == 'MultiPolygon': parts = [r for p in c for r in p]
        elif t == 'LineString': parts = [c]
        elif t == 'MultiLineString': parts = c
        else: continue
        for ring in parts:
            pts = [(float(x), float(y)) for x, y, *_ in ring]
            if len(pts) >= 2: out.append(pts)
    return out

def write(path, ls):
    with open(path, 'wb') as f:
        f.write(struct.pack('<I', len(ls)))
        for l in ls:
            f.write(struct.pack('<H', len(l)))
            for x, y in l:
                f.write(struct.pack('<hh', round(max(-180, min(180, x)) * 100), round(max(-90, min(90, y)) * 100)))

land, borders, out = sys.argv[1], sys.argv[2], sys.argv[3]
L = lines(land, True); B = lines(borders, False)
write(os.path.join(out, 'land.bin'), L); write(os.path.join(out, 'borders.bin'), B)
print(len(L), 'land rings', sum(map(len, L)), 'pts;', len(B), 'border lines', sum(map(len, B)), 'pts')
