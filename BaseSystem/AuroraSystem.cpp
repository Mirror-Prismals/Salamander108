#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace AuroraSystemLogic {

    enum class RibbonBlendMode {
        Alpha,
        SrcAlphaOne
    };

    struct RibbonLayer { float alpha; float scaleX; float scaleY; RibbonBlendMode blendMode = RibbonBlendMode::Alpha; };

    static float frand() { return static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) + 1.0f); }

    namespace {
        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool parseBool(const std::string& value, bool fallback) {
            const std::string lower = toLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
            return fallback;
        }

        bool registryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) return parseBool(std::get<std::string>(it->second), fallback);
            return fallback;
        }

        void setRegistryString(BaseSystem& baseSystem, const std::string& key, const std::string& value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = value;
        }

        bool renderToWaterSceneTarget(const RendererContext& renderer) {
            return renderer.waterCompositeShader
                && renderer.waterSceneFBO != 0
                && renderer.waterSceneTex != 0
                && renderer.waterSceneWidth > 0
                && renderer.waterSceneHeight > 0;
        }

        bool aurorasEnabled(const BaseSystem& baseSystem) {
            return registryBool(
                baseSystem,
                "AuroraRenderEnabled",
                registryBool(baseSystem, "AuroraEnabled", true)
            );
        }

        glm::vec3 activeCameraPosition(const BaseSystem& baseSystem) {
            glm::vec3 cameraPos = baseSystem.player ? baseSystem.player->cameraPosition : glm::vec3(0.0f);
            if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
                const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
                if (glm::length(securityCamera.viewForward) > 1e-4f) {
                    cameraPos = securityCamera.viewPosition;
                }
            }
            return cameraPos;
        }

        void resolveViewProjection(const BaseSystem& baseSystem,
                                   glm::mat4& outView,
                                   glm::mat4& outProjection) {
            const PlayerContext& player = *baseSystem.player;
            outView = player.viewMatrix;
            outProjection = player.projectionMatrix;
            if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
                const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
                if (glm::length(securityCamera.viewForward) > 1e-4f) {
                    outView = glm::lookAt(
                        securityCamera.viewPosition,
                        securityCamera.viewPosition + glm::normalize(securityCamera.viewForward),
                        glm::vec3(0.0f, 1.0f, 0.0f)
                    );
                }
            }
        }

        glm::vec3 paletteColor(int palette, size_t layerIndex, float seed) {
            const glm::vec3 magenta[3] = {
                glm::vec3(1.00f, 0.17f, 0.72f),
                glm::vec3(0.80f, 0.36f, 1.00f),
                glm::vec3(0.42f, 0.76f, 1.00f)
            };
            const glm::vec3 lime[3] = {
                glm::vec3(0.15f, 1.00f, 0.28f),
                glm::vec3(0.08f, 0.88f, 0.62f),
                glm::vec3(0.44f, 0.95f, 1.00f)
            };
            const glm::vec3* colors = (palette == 0) ? magenta : lime;
            const size_t index = std::min(layerIndex, static_cast<size_t>(2));
            const glm::vec3 next = colors[(index + 1u) % 3u];
            return glm::mix(colors[index], next, 0.18f + 0.18f * seed);
        }

        void publishAuroraDebug(BaseSystem& baseSystem, bool active, const char* reason) {
            std::ostringstream ss;
            ss << "AURORA: active=" << (active ? "true" : "false");
            if (reason && *reason) ss << " reason=" << reason;
            setRegistryString(baseSystem, "AuroraDebugState", ss.str());
        }
    }

    static void buildStaticRibbonMesh(RendererContext::AuroraRibbon& a, IRenderBackend& renderBackend) {
        constexpr int hSeg = 96;
        constexpr int vSeg = 10;
        std::vector<float> verts;
        verts.reserve((hSeg - 1) * (vSeg - 1) * 6 * 5);

        auto emit = [&](float uCoord, float vCoord) {
            verts.push_back(uCoord);
            verts.push_back(vCoord);
            verts.push_back(0.0f);
            verts.push_back(uCoord);
            verts.push_back(vCoord);
        };

        for (int i = 0; i < hSeg - 1; ++i) {
            float u0 = static_cast<float>(i) / static_cast<float>(hSeg - 1);
            float u1 = static_cast<float>(i + 1) / static_cast<float>(hSeg - 1);
            for (int j = 0; j < vSeg - 1; ++j) {
                float v0 = static_cast<float>(j) / static_cast<float>(vSeg - 1);
                float v1 = static_cast<float>(j + 1) / static_cast<float>(vSeg - 1);

                emit(u0, v0);
                emit(u1, v0);
                emit(u1, v1);

                emit(u0, v0);
                emit(u1, v1);
                emit(u0, v1);
            }
        }

        a.vertexCount = static_cast<int>(verts.size() / 5);
        renderBackend.uploadArrayBufferData(a.vbo, verts.data(), verts.size() * sizeof(float), false);
    }

    static void initAuroraResources(BaseSystem& baseSystem, IRenderBackend& renderBackend) {
        if (!baseSystem.renderer || !baseSystem.world) return;
        RendererContext& renderer = *baseSystem.renderer;
        if (!renderer.auroraShader) {
            static bool warnedMissingShaders = false;
            const auto vertexIt = baseSystem.world->shaders.find("AURORA_VERTEX_SHADER");
            const auto fragmentIt = baseSystem.world->shaders.find("AURORA_FRAGMENT_SHADER");
            if (vertexIt == baseSystem.world->shaders.end() || fragmentIt == baseSystem.world->shaders.end()) {
                if (!warnedMissingShaders) {
                    std::cerr << "AuroraSystem: missing shader sources." << std::endl;
                    warnedMissingShaders = true;
                }
                return;
            }
            renderer.auroraShader = std::make_unique<Shader>(
                vertexIt->second.c_str(),
                fragmentIt->second.c_str()
            );
            if (!renderer.auroraShader->isValid()) {
                std::cerr << "AuroraSystem: failed to create shader." << std::endl;
                renderer.auroraShader.reset();
                return;
            }
        }
        if (renderer.auroras.empty()) {
            renderer.auroras.resize(4);
            static const std::vector<VertexAttribLayout> kAuroraLayout = {
                {0, 3, VertexAttribType::Float, false, static_cast<unsigned int>(5u * sizeof(float)), 0, 0},
                {1, 1, VertexAttribType::Float, false, static_cast<unsigned int>(5u * sizeof(float)), 3u * sizeof(float), 0},
                {2, 1, VertexAttribType::Float, false, static_cast<unsigned int>(5u * sizeof(float)), 4u * sizeof(float), 0}
            };
            for (size_t i = 0; i < renderer.auroras.size(); ++i) {
                float ang = static_cast<float>(i) / 4.0f * 6.283185307f + 0.2f * (frand() - 0.5f);
                float dist = 220.0f + 120.0f * static_cast<float>(i % 2);
                float height = 400.0f + ((i % 2) ? 60.0f : 0.0f);
                renderer.auroras[i].pos = glm::vec3(cos(ang) * dist, height, sin(ang) * dist);
                renderer.auroras[i].yaw = ang + 1.57079632679f;
                renderer.auroras[i].width = 1200.0f + 400.0f * ((i % 2) ? 1.0f : 0.0f);
                renderer.auroras[i].height = 140.0f + 60.0f * ((i % 2) ? 0.7f : 0.0f);
                renderer.auroras[i].palette = (i < 2) ? 0 : 1;
                renderer.auroras[i].bend = 40.0f + frand() * 200.0f;
                renderer.auroras[i].seed = frand();
                renderBackend.ensureVertexArray(renderer.auroras[i].vao);
                renderBackend.ensureArrayBuffer(renderer.auroras[i].vbo);
                renderBackend.configureVertexArray(renderer.auroras[i].vao, renderer.auroras[i].vbo, kAuroraLayout, 0, {});
                buildStaticRibbonMesh(renderer.auroras[i], renderBackend);
            }
        }
        for (auto& aurora : renderer.auroras) {
            if (aurora.vertexCount <= 0) {
                buildStaticRibbonMesh(aurora, renderBackend);
            }
        }
    }

    void RenderAuroras(BaseSystem& baseSystem, float time, const glm::mat4& view, const glm::mat4& projection) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend || !baseSystem.player) return;
        RendererContext& renderer = *baseSystem.renderer;
        const PlayerContext& player = *baseSystem.player;
        auto& renderBackend = *baseSystem.renderBackend;
        initAuroraResources(baseSystem, renderBackend);
        if (!renderer.auroraShader) return;
        const glm::vec3 cameraAnchor(activeCameraPosition(baseSystem).x, 0.0f, activeCameraPosition(baseSystem).z);

        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setDepthWriteEnabled = [&](bool enabled) {
            renderBackend.setDepthWriteEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };
        auto setBlendModeSrcAlphaOne = [&]() {
            renderBackend.setBlendModeSrcAlphaOne();
        };
        auto setCullEnabled = [&](bool enabled) {
            renderBackend.setCullEnabled(enabled);
        };

        setDepthTestEnabled(true);
        setDepthWriteEnabled(false);
        setBlendEnabled(true);
        setBlendModeAlpha();
        setCullEnabled(false);

        renderer.auroraShader->use();
        renderer.auroraShader->setMat4("view", view);
        renderer.auroraShader->setMat4("projection", projection);
        PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.auroraShader);
        renderer.auroraShader->setFloat("time", time);

        static constexpr std::array<RibbonLayer, 2> layers = {{
            {0.82f, 1.0f, 1.0f, RibbonBlendMode::Alpha},
            {0.34f, 1.18f, 1.28f, RibbonBlendMode::SrcAlphaOne}
        }};

        for (size_t li = 0; li < layers.size(); ++li) {
            const auto& layer = layers[li];
            for (auto& a : renderer.auroras) {
                if (a.vertexCount <= 0) continue;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), cameraAnchor + a.pos);
                model = glm::rotate(model, a.yaw, glm::vec3(0,1,0));
                model = glm::scale(model, glm::vec3(a.width * layer.scaleX, a.height * layer.scaleY, a.width * 0.12f));

                if (layer.blendMode == RibbonBlendMode::Alpha) {
                    setBlendModeAlpha();
                } else if (layer.blendMode == RibbonBlendMode::SrcAlphaOne) {
                    setBlendModeSrcAlphaOne();
                } else {
                    setBlendModeAlpha();
                }
                renderer.auroraShader->setMat4("model", model);
                renderer.auroraShader->setVec3("color", paletteColor(a.palette, li, a.seed));
                renderer.auroraShader->setFloat("passAlpha", layer.alpha);
                renderer.auroraShader->setFloat("auroraDensity", a.bend);
                renderer.auroraShader->setFloat("auroraWeight", a.seed);
                renderer.auroraShader->setFloat("auroraDecay", static_cast<float>(li));
                renderBackend.bindVertexArray(a.vao);
                renderBackend.drawArraysTriangles(0, a.vertexCount);
            }
        }

        renderBackend.unbindVertexArray();
        setCullEnabled(true);
        setBlendEnabled(false);
        setDepthWriteEnabled(true);
        setDepthTestEnabled(true);
    }

    void RenderAuroras(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.renderBackend) return;
        if (!aurorasEnabled(baseSystem)) {
            publishAuroraDebug(baseSystem, false, "disabled");
            return;
        }

        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        const bool renderToSceneTarget = renderToWaterSceneTarget(renderer);
        if (renderToSceneTarget) {
            renderBackend.resumeOffscreenColorPass(
                renderer.waterSceneFBO,
                renderer.waterSceneWidth,
                renderer.waterSceneHeight
            );
        }

        glm::mat4 view(1.0f);
        glm::mat4 projection(1.0f);
        resolveViewProjection(baseSystem, view, projection);
        RenderAuroras(baseSystem, static_cast<float>(PlatformInput::GetTimeSeconds()), view, projection);
        publishAuroraDebug(baseSystem, renderer.auroraShader ? true : false, renderer.auroraShader ? "" : "missing_shader");

        if (renderToSceneTarget) {
            renderBackend.endOffscreenColorPass();
        }
    }

}
