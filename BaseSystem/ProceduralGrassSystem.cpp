#pragma once

#include "../Host.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ProceduralGrassSystemLogic {
    constexpr int kProceduralGrassTileIndex = -30;
    namespace {
        constexpr int CELL = 48;
        constexpr int GRID = 4;
        constexpr int WORLD_SEED = 909;
        constexpr double PI = 3.141592653589793238462643383279502884;

        struct RGB {
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
        };

        constexpr std::array<RGB, 8> PALETTE{{
            {2, 20, 5},
            {4, 36, 8},
            {7, 58, 12},
            {13, 86, 17},
            {26, 122, 24},
            {48, 165, 32},
            {90, 220, 42},
            {145, 255, 58},
        }};

        int clampInt(int v, int lo, int hi) {
            return std::max(lo, std::min(hi, v));
        }

        RGB color(int i) {
            return PALETTE[static_cast<std::size_t>(clampInt(i, 0, static_cast<int>(PALETTE.size()) - 1))];
        }

        int hash_seed(int x, int y, int salt = 0) {
            return WORLD_SEED + x * 928371 + y * 123721 + salt * 1777;
        }

        class PythonRandom {
        public:
            explicit PythonRandom(std::int64_t seed) {
                seed_abs(seed);
            }

            double random() {
                const std::uint32_t a = genrand_uint32() >> 5;
                const std::uint32_t b = genrand_uint32() >> 6;
                return (static_cast<double>(a) * 67108864.0 + static_cast<double>(b)) *
                       (1.0 / 9007199254740992.0);
            }

            double uniform(double a, double b) {
                return a + (b - a) * random();
            }

            int randrange(int stop) {
                if (stop <= 0) {
                    throw std::invalid_argument("empty range for randrange");
                }
                return randbelow(stop);
            }

            int randrange(int start, int stop) {
                if (stop <= start) {
                    throw std::invalid_argument("empty range for randrange");
                }
                return start + randbelow(stop - start);
            }

            int randint(int a, int b) {
                return randrange(a, b + 1);
            }

            int choice(std::initializer_list<int> values) {
                const auto n = static_cast<int>(values.size());
                const int index = randbelow(n);
                auto it = values.begin();
                std::advance(it, index);
                return *it;
            }

        private:
            static constexpr int N = 624;
            static constexpr int M = 397;
            static constexpr std::uint32_t MATRIX_A = 0x9908b0dfU;
            static constexpr std::uint32_t UPPER_MASK = 0x80000000U;
            static constexpr std::uint32_t LOWER_MASK = 0x7fffffffU;

            std::array<std::uint32_t, N> mt{};
            int index = N + 1;

            void seed_abs(std::int64_t seed) {
                std::uint64_t value = seed < 0 ? static_cast<std::uint64_t>(-seed) : static_cast<std::uint64_t>(seed);
                std::vector<std::uint32_t> key;
                do {
                    key.push_back(static_cast<std::uint32_t>(value & 0xffffffffULL));
                    value >>= 32;
                } while (value != 0);
                init_by_array(key);
            }

            void init_genrand(std::uint32_t s) {
                mt[0] = s;
                for (index = 1; index < N; ++index) {
                    mt[static_cast<std::size_t>(index)] =
                        1812433253U *
                            (mt[static_cast<std::size_t>(index - 1)] ^
                             (mt[static_cast<std::size_t>(index - 1)] >> 30)) +
                        static_cast<std::uint32_t>(index);
                }
            }

            void init_by_array(const std::vector<std::uint32_t>& key) {
                init_genrand(19650218U);

                int i = 1;
                int j = 0;
                int k = std::max(N, static_cast<int>(key.size()));

                for (; k > 0; --k) {
                    mt[static_cast<std::size_t>(i)] =
                        (mt[static_cast<std::size_t>(i)] ^
                         ((mt[static_cast<std::size_t>(i - 1)] ^
                           (mt[static_cast<std::size_t>(i - 1)] >> 30)) *
                          1664525U)) +
                        key[static_cast<std::size_t>(j)] + static_cast<std::uint32_t>(j);
                    ++i;
                    ++j;
                    if (i >= N) {
                        mt[0] = mt[N - 1];
                        i = 1;
                    }
                    if (j >= static_cast<int>(key.size())) {
                        j = 0;
                    }
                }

                for (k = N - 1; k > 0; --k) {
                    mt[static_cast<std::size_t>(i)] =
                        (mt[static_cast<std::size_t>(i)] ^
                         ((mt[static_cast<std::size_t>(i - 1)] ^
                           (mt[static_cast<std::size_t>(i - 1)] >> 30)) *
                          1566083941U)) -
                        static_cast<std::uint32_t>(i);
                    ++i;
                    if (i >= N) {
                        mt[0] = mt[N - 1];
                        i = 1;
                    }
                }

                mt[0] = 0x80000000U;
                index = N;
            }

            std::uint32_t genrand_uint32() {
                static constexpr std::array<std::uint32_t, 2> mag01{{0x0U, MATRIX_A}};

                if (index >= N) {
                    int kk = 0;
                    for (; kk < N - M; ++kk) {
                        const std::uint32_t y = (mt[static_cast<std::size_t>(kk)] & UPPER_MASK) |
                                                (mt[static_cast<std::size_t>(kk + 1)] & LOWER_MASK);
                        mt[static_cast<std::size_t>(kk)] =
                            mt[static_cast<std::size_t>(kk + M)] ^ (y >> 1) ^ mag01[y & 0x1U];
                    }
                    for (; kk < N - 1; ++kk) {
                        const std::uint32_t y = (mt[static_cast<std::size_t>(kk)] & UPPER_MASK) |
                                                (mt[static_cast<std::size_t>(kk + 1)] & LOWER_MASK);
                        mt[static_cast<std::size_t>(kk)] =
                            mt[static_cast<std::size_t>(kk + (M - N))] ^ (y >> 1) ^ mag01[y & 0x1U];
                    }
                    const std::uint32_t y = (mt[N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
                    mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
                    index = 0;
                }

                std::uint32_t y = mt[static_cast<std::size_t>(index++)];
                y ^= y >> 11;
                y ^= (y << 7) & 0x9d2c5680U;
                y ^= (y << 15) & 0xefc60000U;
                y ^= y >> 18;
                return y;
            }

            std::uint32_t getrandbits(int k) {
                if (k <= 0 || k > 32) {
                    throw std::invalid_argument("getrandbits only supports 1..32 bits");
                }
                return genrand_uint32() >> (32 - k);
            }

            int randbelow(int n) {
                const int k = bit_length(n);
                std::uint32_t r = 0;
                do {
                    r = getrandbits(k);
                } while (r >= static_cast<std::uint32_t>(n));
                return static_cast<int>(r);
            }

            static int bit_length(int n) {
                int bits = 0;
                while (n > 0) {
                    ++bits;
                    n >>= 1;
                }
                return bits;
            }
        };

        struct Image {
            int width;
            int height;
            std::vector<std::uint8_t> pixels;

            Image(int w, int h, RGB fill = {0, 0, 0})
                : width(w), height(h), pixels(static_cast<std::size_t>(w * h * 3)) {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        set(x, y, fill);
                    }
                }
            }

            void set(int x, int y, RGB c) {
                const std::size_t offset = static_cast<std::size_t>((y * width + x) * 3);
                pixels[offset + 0] = c.r;
                pixels[offset + 1] = c.g;
                pixels[offset + 2] = c.b;
            }

            RGB get(int x, int y) const {
                const std::size_t offset = static_cast<std::size_t>((y * width + x) * 3);
                return {pixels[offset + 0], pixels[offset + 1], pixels[offset + 2]};
            }
        };

        struct Center {
            double x;
            double y;
            double strength;
        };

        int wrap_cell(int v) {
            const int r = v % CELL;
            return r < 0 ? r + CELL : r;
        }

        int py_round_to_int(double v) {
            return static_cast<int>(std::nearbyint(v));
        }

        std::vector<Center> shared_edge_centers(int cx, int cy) {
            std::vector<Center> centers;
            centers.reserve(12);

            PythonRandom interior(hash_seed(cx, cy, 50));
            for (int i = 0; i < 4; ++i) {
                centers.push_back({
                    static_cast<double>(interior.randrange(6, CELL - 6)),
                    static_cast<double>(interior.randrange(6, CELL - 6)),
                    interior.uniform(5.0, 8.0),
                });
            }

            const std::array<std::pair<int, int>, 2> xEdges{{{0, cx - 1}, {CELL, cx}}};
            for (const auto& [side_x, nx] : xEdges) {
                PythonRandom rng(hash_seed(nx, cy, 100));
                for (int i = 0; i < 2; ++i) {
                    centers.push_back({
                        static_cast<double>(side_x) + rng.uniform(-4.0, 4.0),
                        static_cast<double>(rng.randrange(4, CELL - 4)),
                        rng.uniform(4.5, 7.0),
                    });
                }
            }

            const std::array<std::pair<int, int>, 2> yEdges{{{0, cy - 1}, {CELL, cy}}};
            for (const auto& [side_y, ny] : yEdges) {
                PythonRandom rng(hash_seed(cx, ny, 200));
                for (int i = 0; i < 2; ++i) {
                    centers.push_back({
                        static_cast<double>(rng.randrange(4, CELL - 4)),
                        static_cast<double>(side_y) + rng.uniform(-4.0, 4.0),
                        rng.uniform(4.5, 7.0),
                    });
                }
            }

            return centers;
        }

        Image make_cell(int cx, int cy) {
            PythonRandom rng(hash_seed(cx, cy));
            Image img(CELL, CELL, PALETTE[1]);
            const std::vector<Center> centers = shared_edge_centers(cx, cy);

            auto flow_angle = [&](int x, int y) {
                double vx = 0.0;
                double vy = -0.75;

                for (const Center& center : centers) {
                    const double dx = static_cast<double>(x) - center.x;
                    const double dy = static_cast<double>(y) - center.y;
                    const double d2 = dx * dx + dy * dy + 12.0;
                    vx += center.strength * dx / d2;
                    vy += center.strength * dy / d2;
                }

                vx += std::sin(static_cast<double>(y) * 0.33 + static_cast<double>(x) * 0.08 + static_cast<double>(cx)) * 0.18;
                vy += std::sin(static_cast<double>(x) * 0.27 + static_cast<double>(cy)) * 0.10;

                return std::atan2(vy, vx);
            };

            auto point = [&](int x, int y, int c) {
                img.set(wrap_cell(x), wrap_cell(y), color(c));
            };

            auto rect = [&](int x1, int y1, int x2, int y2, int c) {
                for (int yy = y1; yy <= y2; ++yy) {
                    for (int xx = x1; xx <= x2; ++xx) {
                        point(xx, yy, c);
                    }
                }
            };

            for (int y = 0; y < CELL; y += 2) {
                for (int x = 0; x < CELL; x += 2) {
                    const double a = flow_angle(x, y);
                    const double wave = std::sin(a * 3.0) + std::sin(static_cast<double>(x) * 0.2 + static_cast<double>(y) * 0.13);
                    const int idx = 2 + static_cast<int>(wave * 0.7) + rng.choice({-1, 0, 0, 1});
                    rect(x, y, x + 1, y + 1, idx);
                }
            }

            auto stamp_streak = [&](int x, int y, int length, int width, int c, double jitter) {
                const double angle = flow_angle(x, y) + rng.uniform(-jitter, jitter);
                const double curve = rng.uniform(-0.018, 0.018);

                for (int i = 0; i < length; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(std::max(1, length - 1));
                    const double a = angle + curve * static_cast<double>(i);

                    const int px = py_round_to_int(static_cast<double>(x) + std::cos(a) * static_cast<double>(i));
                    const int py = py_round_to_int(static_cast<double>(y) + std::sin(a) * static_cast<double>(i));
                    const int w = std::max(1, py_round_to_int(static_cast<double>(width) * (1.0 - t * 0.75)));

                    int cc = c;
                    if (t < 0.18) {
                        cc -= 2;
                    } else if (t < 0.38) {
                        cc -= 1;
                    } else if (t > 0.65) {
                        cc += 1;
                    }
                    if (t > 0.86) {
                        cc += 1;
                    }

                    if (w == 1) {
                        point(px, py, cc);
                    } else {
                        rect(px, py, px + w - 1, py + 1, cc);
                    }

                    if (rng.random() < 0.16) {
                        point(px + rng.choice({-1, 1}), py, cc - 1);
                    }
                }
            };

            auto shadow_cut = [&](int x, int y, int length) {
                const double angle = flow_angle(x, y) + PI + rng.uniform(-0.4, 0.4);

                for (int i = 0; i < length; ++i) {
                    const int px = py_round_to_int(static_cast<double>(x) + std::cos(angle) * static_cast<double>(i));
                    const int py = py_round_to_int(static_cast<double>(y) + std::sin(angle) * static_cast<double>(i));
                    rect(px, py, px + rng.choice({0, 1}), py, rng.choice({0, 1}));
                }
            };

            for (int i = 0; i < 45; ++i) {
                stamp_streak(
                    rng.randrange(CELL), rng.randrange(CELL),
                    rng.randint(10, 22),
                    rng.choice({2, 3}),
                    rng.choice({1, 2, 2, 3}),
                    0.28);
            }

            for (int i = 0; i < 70; ++i) {
                stamp_streak(
                    rng.randrange(CELL), rng.randrange(CELL),
                    rng.randint(10, 24),
                    rng.choice({2, 2, 3}),
                    rng.choice({3, 4, 4, 5}),
                    0.22);
            }

            for (int i = 0; i < 115; ++i) {
                stamp_streak(
                    rng.randrange(CELL), rng.randrange(CELL),
                    rng.randint(5, 14),
                    rng.choice({1, 1, 2}),
                    rng.choice({3, 4, 5, 5, 6}),
                    0.30);
            }

            for (int i = 0; i < 42; ++i) {
                stamp_streak(
                    rng.randrange(CELL), rng.randrange(CELL),
                    rng.randint(4, 10),
                    1,
                    rng.choice({5, 6, 6, 7}),
                    0.20);
            }

            for (int i = 0; i < 65; ++i) {
                shadow_cut(
                    rng.randrange(CELL),
                    rng.randrange(CELL),
                    rng.randint(3, 9));
            }

            for (int i = 0; i < 30; ++i) {
                const int x = rng.randrange(CELL);
                const int y = rng.randrange(CELL);
                rect(
                    x,
                    y,
                    x + rng.choice({1, 2, 3}),
                    y + rng.choice({1, 2}),
                    rng.choice({0, 1}));
            }

            return img;
        }

        Image make_field(int grid = GRID) {
            Image field(CELL * grid, CELL * grid);

            for (int gy = 0; gy < grid; ++gy) {
                for (int gx = 0; gx < grid; ++gx) {
                    const Image cell = make_cell(gx, gy);
                    for (int y = 0; y < CELL; ++y) {
                        for (int x = 0; x < CELL; ++x) {
                            field.set(gx * CELL + x, gy * CELL + y, cell.get(x, y));
                        }
                    }
                }
            }

            return field;
        }

        std::vector<unsigned char> make_field_rgba() {
            const Image field = make_field(GRID);
            std::vector<unsigned char> rgba(static_cast<size_t>(field.width * field.height * 4), 255u);
            for (int y = 0; y < field.height; ++y) {
                for (int x = 0; x < field.width; ++x) {
                    const RGB px = field.get(x, y);
                    const size_t offset = static_cast<size_t>((y * field.width + x) * 4);
                    rgba[offset + 0] = px.r;
                    rgba[offset + 1] = px.g;
                    rgba[offset + 2] = px.b;
                    rgba[offset + 3] = 255u;
                }
            }
            return rgba;
        }
    }

    bool IsProceduralGrassPrototypeName(const std::string& name) {
        return name == "GrassBlockProceduralBiomeTex";
    }

    int ProceduralGrassTileIndex() {
        return kProceduralGrassTileIndex;
    }

    void UpdateProceduralGrassMaterial(BaseSystem& baseSystem,
                                       std::vector<Entity>& prototypes,
                                       float dt,
                                       PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        if (renderer.proceduralGrassTexture != 0
            && renderer.proceduralGrassTextureSize.x == CELL * GRID
            && renderer.proceduralGrassTextureSize.y == CELL * GRID) {
            return;
        }

        TextureUploadParams params;
        params.minFilter = TextureFilterMode::Nearest;
        params.magFilter = TextureFilterMode::Nearest;
        params.wrapS = TextureWrapMode::ClampToEdge;
        params.wrapT = TextureWrapMode::ClampToEdge;

        const std::vector<unsigned char> pixels = make_field_rgba();
        if (baseSystem.renderBackend->uploadRgbaTexture2D(
                renderer.proceduralGrassTexture,
                CELL * GRID,
                CELL * GRID,
                pixels,
                params)) {
            renderer.proceduralGrassTextureSize = glm::ivec2(CELL * GRID, CELL * GRID);
            std::cout << "ProceduralGrassSystem: generated runtime grass texture "
                      << renderer.proceduralGrassTextureSize.x << "x"
                      << renderer.proceduralGrassTextureSize.y << ".\n";
        } else {
            std::cerr << "ProceduralGrassSystem: failed to upload runtime grass texture.\n";
        }
    }
}
