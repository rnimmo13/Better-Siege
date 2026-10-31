// Regenerate: in PowerShell, Add-Type -Path ToolsTexGen.cs -ReferencedAssemblies System.Drawing; [TexGen]::Generate("<out dir>")
// then import the PNGs into /Game/Siege/Textures/Env (normal maps: TC_Normalmap, sRGB off). Wood ships as T_Wood2 (GenerateWood).
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

// Procedural, tileable environment textures for the Embassy.
// Each texture writes <name>_D.png (RGB albedo, A roughness) and <name>_N.png (tangent-space normal from height).
public static class TexGen
{
    const int N = 1024;

    // ---------- tileable value noise ----------
    static uint Hash(int x, int y, int seed)
    {
        unchecked
        {
            uint h = (uint)(x * 374761393 + y * 668265263 + seed * 2147483647);
            h = (h ^ (h >> 13)) * 1274126177u;
            return h ^ (h >> 16);
        }
    }

    static float Rand(int x, int y, int seed) { return (Hash(x, y, seed) & 0xFFFFFF) / 16777215f; }

    static float Smooth(float t) { return t * t * (3 - 2 * t); }

    // value noise with integer period so it tiles
    static float Value(float x, float y, int period, int seed)
    {
        int x0 = (int)Math.Floor(x), y0 = (int)Math.Floor(y);
        float fx = Smooth(x - x0), fy = Smooth(y - y0);
        int ax = ((x0 % period) + period) % period, ay = ((y0 % period) + period) % period;
        int bx = (ax + 1) % period, by = (ay + 1) % period;
        float a = Rand(ax, ay, seed), b = Rand(bx, ay, seed), c = Rand(ax, by, seed), d = Rand(bx, by, seed);
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
    }

    // fractal noise over the unit square (u,v in 0..1), tiles at the edges
    static float Fbm(float u, float v, int baseFreq, int octaves, int seed)
    {
        float sum = 0, amp = 0.5f, norm = 0;
        int f = baseFreq;
        for (int o = 0; o < octaves; o++)
        {
            sum += Value(u * f, v * f, f, seed + o * 31) * amp;
            norm += amp;
            amp *= 0.5f;
            f *= 2;
        }
        return sum / norm;
    }

    // value noise with separate X and Y periods, for stretched grain
    static float ValueXY(float x, float y, int px, int py, int seed)
    {
        int x0 = (int)Math.Floor(x), y0 = (int)Math.Floor(y);
        float fx = Smooth(x - x0), fy = Smooth(y - y0);
        int ax = ((x0 % px) + px) % px, ay = ((y0 % py) + py) % py;
        int bx = (ax + 1) % px, by = (ay + 1) % py;
        float a = Rand(ax, ay, seed), b = Rand(bx, ay, seed), c = Rand(ax, by, seed), d = Rand(bx, by, seed);
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
    }

    static float FbmXY(float u, float v, int fx, int fy, int octaves, int seed)
    {
        float sum = 0, amp = 0.5f, norm = 0;
        for (int o = 0; o < octaves; o++)
        {
            sum += ValueXY(u * fx, v * fy, fx, fy, seed + o * 31) * amp;
            norm += amp; amp *= 0.5f; fx *= 2; fy *= 2;
        }
        return sum / norm;
    }

    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    delegate void Shader(float u, float v, out float r, out float g, out float b, out float rough, out float height);

    static void Render(string dir, string name, Shader shader, float normalStrength)
    {
        float[] R = new float[N * N], G = new float[N * N], B = new float[N * N], Ro = new float[N * N], H = new float[N * N];
        System.Threading.Tasks.Parallel.For(0, N, y =>
        {
            for (int x = 0; x < N; x++)
            {
                float r, g, b, ro, h;
                shader((x + 0.5f) / N, (y + 0.5f) / N, out r, out g, out b, out ro, out h);
                int i = y * N + x;
                R[i] = r; G[i] = g; B[i] = b; Ro[i] = ro; H[i] = h;
            }
        });

        byte[] diff = new byte[N * N * 4];
        byte[] norm = new byte[N * N * 4];
        for (int y = 0; y < N; y++)
        {
            for (int x = 0; x < N; x++)
            {
                int i = y * N + x;
                // BGRA order for 32bpp ARGB bitmaps
                diff[i * 4 + 0] = (byte)(Clamp01(B[i]) * 255);
                diff[i * 4 + 1] = (byte)(Clamp01(G[i]) * 255);
                diff[i * 4 + 2] = (byte)(Clamp01(R[i]) * 255);
                diff[i * 4 + 3] = (byte)(Clamp01(Ro[i]) * 255);

                int xl = (x + N - 1) % N, xr = (x + 1) % N, yu = (y + N - 1) % N, yd = (y + 1) % N;
                float dx = (H[y * N + xr] - H[y * N + xl]) * normalStrength;
                float dy = (H[yd * N + x] - H[yu * N + x]) * normalStrength;
                float nx = -dx, ny = dy, nz = 1;
                float len = (float)Math.Sqrt(nx * nx + ny * ny + nz * nz);
                nx /= len; ny /= len; nz /= len;
                norm[i * 4 + 0] = (byte)((nz * 0.5f + 0.5f) * 255);
                norm[i * 4 + 1] = (byte)((ny * 0.5f + 0.5f) * 255);
                norm[i * 4 + 2] = (byte)((nx * 0.5f + 0.5f) * 255);
                norm[i * 4 + 3] = 255;
            }
        }

        Save(diff, dir + "\\T_" + name + "_D.png");
        Save(norm, dir + "\\T_" + name + "_N.png");
    }

    static void Save(byte[] data, string path)
    {
        using (var bmp = new Bitmap(N, N, PixelFormat.Format32bppArgb))
        {
            var bd = bmp.LockBits(new Rectangle(0, 0, N, N), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(data, 0, bd.Scan0, data.Length);
            bmp.UnlockBits(bd);
            bmp.Save(path, ImageFormat.Png);
        }
    }

    // ---------- materials ----------

    // running-bond bricks; the texture covers 120cm: 5 bricks across, 16 courses
    static void Brick(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        const int Cols = 5, Rows = 16;
        float fy = v * Rows; int row = (int)fy;
        float fx = u * Cols + ((row & 1) == 1 ? 0.5f : 0f);
        int col = (int)Math.Floor(fx);
        float lx = fx - col, ly = fy - row;
        int colWrapped = ((col % Cols) + Cols) % Cols;

        float mortarX = 0.035f, mortarY = 0.11f;
        float edge = Math.Min(Math.Min(lx, 1 - lx) / mortarX, Math.Min(ly, 1 - ly) / mortarY);
        float brickMask = Clamp01((edge - 1f) * 3f);

        float tone = Rand(colWrapped, row, 7);
        float grain = Fbm(u, v, 32, 4, 11);
        float br = Lerp(0.42f, 0.58f, tone) * (0.85f + grain * 0.3f);
        float bg = Lerp(0.17f, 0.25f, tone) * (0.85f + grain * 0.3f);
        float bb = Lerp(0.12f, 0.17f, tone) * (0.85f + grain * 0.3f);
        // a few darker, burnt bricks
        if (Rand(colWrapped, row, 99) > 0.88f) { br *= 0.6f; bg *= 0.6f; bb *= 0.65f; }

        float m = 0.55f + Fbm(u, v, 64, 3, 5) * 0.15f;
        r = Lerp(m, br, brickMask); g = Lerp(m * 0.97f, bg, brickMask); b = Lerp(m * 0.92f, bb, brickMask);
        ro = Lerp(0.95f, 0.8f, brickMask);
        h = brickMask * (0.8f + grain * 0.2f);
    }

    // painted plaster; covers 200cm
    static void Plaster(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        float n = Fbm(u, v, 8, 6, 21);
        float fine = Fbm(u, v, 128, 2, 22);
        float c = 0.80f + (n - 0.5f) * 0.08f + (fine - 0.5f) * 0.04f;
        r = c; g = c * 0.985f; b = c * 0.96f;
        ro = 0.85f;
        h = n * 0.6f + fine * 0.4f;
    }

    // oak planks; covers 200cm: 10 planks of 20cm, boards 100cm long staggered
    static void Wood(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        const int Planks = 10;
        float fx = u * Planks; int plank = (int)fx; float lx = fx - plank;
        float offset = Rand(plank, 0, 3);
        float fy = v * 2f + offset; int board = (int)Math.Floor(fy); float ly = fy - board;
        int boardWrapped = ((board % 2) + 2) % 2;

        float seam = Math.Min(Math.Min(lx, 1 - lx) / 0.02f, Math.Min(ly, 1 - ly) / 0.006f);
        float seamMask = Clamp01(seam - 0.5f);

        float tone = Rand(plank, boardWrapped, 13);
        // stretched noise gives grain along the plank
        float grain = FbmXY(u, v, 80, 3, 4, 31);
        float rings = (float)Math.Sin((Fbm(u, v, 2, 3, 41) * 40f + u * Planks * 6f) * 3.14159f) * 0.5f + 0.5f;
        float streak = FbmXY(u, v, 160, 4, 3, 51);
        float baseC = Lerp(0.38f, 0.55f, tone) * (0.9f + rings * 0.05f + streak * 0.1f);
        r = baseC; g = baseC * 0.68f; b = baseC * 0.42f;
        r *= Lerp(0.3f, 1f, seamMask); g *= Lerp(0.3f, 1f, seamMask); b *= Lerp(0.3f, 1f, seamMask);
        ro = Lerp(0.9f, 0.45f + grain * 0.15f, seamMask);
        h = seamMask * (0.97f + rings * 0.03f);
    }

    // large ceramic floor tiles; covers 120cm: 2x2 tiles of 60cm
    static void Tile(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        const int T = 2;
        float fx = u * T, fy = v * T; int tx = (int)fx, ty = (int)fy;
        float lx = fx - tx, ly = fy - ty;
        float edge = Math.Min(Math.Min(lx, 1 - lx), Math.Min(ly, 1 - ly)) / 0.012f;
        float tileMask = Clamp01(edge - 1f);
        float tone = Rand(tx, ty, 61);
        float marble = Fbm(u, v, 4, 6, 71);
        float vein = (float)Math.Pow(1f - Math.Abs(Fbm(u, v, 3, 5, 81) - 0.5f) * 2f, 12);
        float c = Lerp(0.74f, 0.82f, tone) * (0.95f + marble * 0.08f) - vein * 0.18f;
        r = Lerp(0.35f, c, tileMask); g = Lerp(0.34f, c * 0.99f, tileMask); b = Lerp(0.32f, c * 0.97f, tileMask);
        ro = Lerp(0.95f, 0.22f, tileMask);
        h = tileMask;
    }

    // cast concrete; covers 300cm, form panel seams every 150cm with tie holes
    static void Concrete(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        float n = Fbm(u, v, 6, 7, 91);
        float pores = Fbm(u, v, 96, 2, 92);
        float pit = pores < 0.22f ? (0.22f - pores) * 3f : 0f;
        float stain = Fbm(u, v, 3, 4, 93);

        float fx = u * 2f, fy = v * 2f;
        float lx = fx - (float)Math.Floor(fx), ly = fy - (float)Math.Floor(fy);
        float seam = Math.Min(Math.Min(lx, 1 - lx), Math.Min(ly, 1 - ly));
        float seamMask = Clamp01(seam / 0.004f);

        // tie holes on a grid
        float hx = u * 4f - (float)Math.Floor(u * 4f) - 0.5f, hy = v * 4f - (float)Math.Floor(v * 4f) - 0.5f;
        float hole = (hx * hx + hy * hy) < 0.0009f ? 1f : 0f;

        float c = 0.5f + (n - 0.5f) * 0.18f - (stain - 0.5f) * 0.12f - pit * 0.4f;
        c *= Lerp(0.7f, 1f, seamMask) * (1f - hole * 0.6f);
        r = c; g = c * 0.99f; b = c * 0.97f;
        ro = 0.9f;
        h = n * 0.5f - pit * 0.6f + seamMask * 0.3f - hole * 0.5f;
    }

    // low-pile office carpet; covers 100cm
    static void Carpet(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        float fine = Fbm(u, v, 256, 2, 101);
        float mid = Fbm(u, v, 16, 3, 102);
        // subtle woven squares
        float sq = ((int)(u * 20) + (int)(v * 20)) % 2 == 0 ? 1f : 0.94f;
        float c = (0.30f + fine * 0.12f + (mid - 0.5f) * 0.05f) * sq;
        r = c * 0.85f; g = c * 0.9f; b = c;
        ro = 1f;
        h = fine;
    }

    // exterior concrete pavers; covers 200cm: 2x2 slabs
    static void Pavers(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        float fx = u * 2f, fy = v * 2f; int px = (int)fx, py = (int)fy;
        float lx = fx - px, ly = fy - py;
        float edge = Math.Min(Math.Min(lx, 1 - lx), Math.Min(ly, 1 - ly)) / 0.01f;
        float mask = Clamp01(edge - 1f);
        float tone = Rand(px, py, 111);
        float n = Fbm(u, v, 8, 6, 112);
        float moss = Clamp01((Fbm(u, v, 4, 5, 113) - 0.55f) * 4f) * (1f - mask * 0.7f);
        float c = Lerp(0.46f, 0.56f, tone) * (0.9f + n * 0.2f);
        r = Lerp(0.18f, c, mask); g = Lerp(0.2f, c, mask); b = Lerp(0.17f, c * 0.97f, mask);
        r = Lerp(r, 0.2f, moss); g = Lerp(g, 0.28f, moss); b = Lerp(b, 0.12f, moss);
        ro = Lerp(1f, 0.85f, mask);
        h = mask * (0.85f + n * 0.15f);
    }

    // brushed sheet metal with panel lines; covers 100cm
    static void Metal(float u, float v, out float r, out float g, out float b, out float ro, out float h)
    {
        float brush = FbmXY(u, v, 4, 512, 2, 121);
        float n = Fbm(u, v, 8, 4, 122);
        float fy = v * 4f; float ly = fy - (float)Math.Floor(fy);
        float line = Clamp01(Math.Min(ly, 1 - ly) / 0.006f);
        float c = (0.55f + brush * 0.12f + (n - 0.5f) * 0.08f) * Lerp(0.5f, 1f, line);
        r = c; g = c; b = c * 1.02f;
        ro = 0.35f + brush * 0.15f;
        h = line * 0.8f + brush * 0.2f;
    }

    public static void GenerateWood(string dir) { Render(dir, "Wood2", Wood, 10f); }

    public static void Generate(string dir)
    {
        Render(dir, "Brick", Brick, 12f);
        Render(dir, "Plaster", Plaster, 1.5f);
        Render(dir, "Wood", Wood, 10f);
        Render(dir, "Tile", Tile, 10f);
        Render(dir, "Concrete", Concrete, 4f);
        Render(dir, "Carpet", Carpet, 2f);
        Render(dir, "Pavers", Pavers, 10f);
        Render(dir, "Metal", Metal, 6f);
    }
}
