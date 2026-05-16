#pragma once

#include <array>
#include <iostream>
#include <vector>

namespace VoxelMeshingSystemLogic { void ResetMeshingRuntime(); }

namespace RenderInitSystemLogic {

    namespace {
        constexpr int kNoGrassVariantEncodeStride = 16384;
        inline int encodeNoGrassVariantTile(int tileIndex) {
            return (tileIndex >= 0) ? (tileIndex + kNoGrassVariantEncodeStride) : tileIndex;
        }
    }

    RenderBehavior BehaviorForPrototype(const Entity& proto) {
        if (proto.name == "Branch") return RenderBehavior::STATIC_BRANCH;
        if (proto.name == "Water") return RenderBehavior::ANIMATED_WATER;
        if (proto.name == "TransparentWave") return RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
        if (proto.hasWireframe && proto.isAnimated) return RenderBehavior::ANIMATED_WIREFRAME;
        return RenderBehavior::STATIC_DEFAULT;
    }

    void DestroyVoxelFaceRenderBuffers(VoxelFaceRenderBuffers& buffers, IRenderBackend& renderBackend) {
        for (size_t i = 0; i < buffers.opaqueVaos.size(); ++i) {
            renderBackend.destroyVertexArray(buffers.opaqueVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.opaqueVBOs[i]);
            buffers.opaqueCounts[i] = 0;
            renderBackend.destroyVertexArray(buffers.clippedOpaqueVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.clippedOpaqueVBOs[i]);
            buffers.clippedOpaqueCounts[i] = 0;
            renderBackend.destroyVertexArray(buffers.alphaVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.alphaVBOs[i]);
            buffers.alphaCounts[i] = 0;
        }
    }

    void DestroyVoxelWaterRenderBuffers(VoxelWaterRenderBuffers& buffers, IRenderBackend& renderBackend) {
        for (size_t i = 0; i < buffers.surfaceVaos.size(); ++i) {
            renderBackend.destroyVertexArray(buffers.surfaceVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.surfaceVBOs[i]);
            buffers.surfaceCounts[i] = 0;
            renderBackend.destroyVertexArray(buffers.bodyVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.bodyVBOs[i]);
            buffers.bodyCounts[i] = 0;
        }
    }

    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType) {
        if (!proto.useTexture) return -1;
        int legacyExternalTile = -1;
        if (proto.textureKey == "DirtExternal") legacyExternalTile = -20;
        if (proto.textureKey == "StoneExternal") legacyExternalTile = -21;
        if (!worldCtx) return legacyExternalTile;
        if (proto.prototypeID < 0 || proto.prototypeID >= static_cast<int>(worldCtx->prototypeTextureSets.size())) return legacyExternalTile;
        const FaceTextureSet& set = worldCtx->prototypeTextureSets[proto.prototypeID];
        auto resolve = [&](int specific) -> int {
            if (specific >= 0) return specific;
            if (set.all >= 0) return set.all;
            return legacyExternalTile;
        };
        int side = resolve(set.side);
        int top = resolve(set.top);
        int bottom = resolve(set.bottom);
        auto resolveMappedTile = [&](const char* textureKey, int fallback) -> int {
            auto it = worldCtx->atlasMappings.find(textureKey);
            if (it == worldCtx->atlasMappings.end()) return fallback;
            const FaceTextureSet& mapped = it->second;
            if (mapped.all >= 0) return mapped.all;
            if (mapped.side >= 0) return mapped.side;
            if (mapped.top >= 0) return mapped.top;
            if (mapped.bottom >= 0) return mapped.bottom;
            return fallback;
        };
        const bool isFirLogX = (proto.name == "FirLog1TexX" || proto.name == "FirLog2TexX");
        const bool isFirLogZ = (proto.name == "FirLog1TexZ" || proto.name == "FirLog2TexZ");
        const bool isStickX = (proto.name == "StickTexX" || proto.name == "StickWinterTexX");
        const bool isStickZ = (proto.name == "StickTexZ" || proto.name == "StickWinterTexZ");
        const bool isWorkbenchPosX = (proto.name == "WorkbenchTexPosX");
        const bool isWorkbenchNegX = (proto.name == "WorkbenchTexNegX");
        const bool isWorkbenchPosZ = (proto.name == "WorkbenchTexPosZ");
        const bool isWorkbenchNegZ = (proto.name == "WorkbenchTexNegZ");
        const bool isLantern = (proto.name == "LanternBlockTex");
        const bool isFirNubX = (proto.name == "FirLog1NubTexX" || proto.name == "FirLog2NubTexX");
        const bool isFirNubZ = (proto.name == "FirLog1NubTexZ" || proto.name == "FirLog2NubTexZ");
        const bool isFirTopY = (proto.name == "FirLog1TopTex" || proto.name == "FirLog2TopTex");
        const bool isCactusX = (proto.name == "Cactus1TexX"
            || proto.name == "Cactus2TexX"
            || proto.name == "Cactus1TexJunctionX"
            || proto.name == "Cactus2TexJunctionX");
        const bool isCactusZ = (proto.name == "Cactus1TexZ"
            || proto.name == "Cactus2TexZ"
            || proto.name == "Cactus1TexJunctionZ"
            || proto.name == "Cactus2TexJunctionZ");
        const bool isComputer = (proto.name == "Computer");
        if (isComputer) {
            const int sideTile = resolveMappedTile("24x24NaviMonitorModernSideRgbaV002", side);
            const int topTile = resolveMappedTile("24x24NaviMonitorModernTopRgbaV002", (top >= 0) ? top : sideTile);
            const int bottomTile = resolveMappedTile("24x24NaviMonitorModernBottomRgbaV002", (bottom >= 0) ? bottom : sideTile);
            const int frontTile = resolveMappedTile("24x24NaviMonitorFrontRgbaModernV001", sideTile);
            const int backTile = resolveMappedTile("24x24NaviMonitorModernBackRgbaV002", sideTile);
            if (faceType == 0) return frontTile;
            if (faceType == 1) return backTile;
            if (faceType == 2) return topTile;
            if (faceType == 3) return bottomTile;
            return sideTile;
        }
        if (isLantern) {
            // User-authored atlas layout:
            // front=304, back=292, side=294, top=295, bottom=293.
            constexpr int kLanternFrontTile = 304;
            constexpr int kLanternBackTile = 292;
            constexpr int kLanternSideTile = 294;
            constexpr int kLanternTopTile = 295;
            constexpr int kLanternBottomTile = 293;
            if (faceType == 0) return kLanternFrontTile;
            if (faceType == 1) return kLanternBackTile;
            if (faceType == 2) return kLanternTopTile;
            if (faceType == 3) return kLanternBottomTile;
            return kLanternSideTile;
        }
        if (isFirNubX) {
            if (faceType == 0 || faceType == 1) return (side >= 0) ? side : top;
            return side;
        }
        if (isFirNubZ) {
            if (faceType == 4 || faceType == 5) return (side >= 0) ? side : top;
            return side;
        }
        if (isFirLogX) {
            if (faceType == 0 || faceType == 1) return (top >= 0) ? top : side;
            return side;
        }
        if (isFirLogZ) {
            if (faceType == 4 || faceType == 5) return (top >= 0) ? top : side;
            return side;
        }
        if (isStickX) {
            if (faceType == 0 || faceType == 1) return (top >= 0) ? top : side;
            return side;
        }
        if (isStickZ) {
            if (faceType == 4 || faceType == 5) return (top >= 0) ? top : side;
            return side;
        }
        if (isCactusX) {
            if (faceType == 0 || faceType == 1) return (top >= 0) ? top : side;
            return side;
        }
        if (isCactusZ) {
            if (faceType == 4 || faceType == 5) return (top >= 0) ? top : side;
            return side;
        }
        if (isWorkbenchPosX || isWorkbenchNegX || isWorkbenchPosZ || isWorkbenchNegZ) {
            if (faceType == 2) return top;
            if (faceType == 3) return bottom;
            const int sideTile = (side >= 0) ? side : top;
            const int frontTile = 276;
            if (isWorkbenchPosX && faceType == 0) return frontTile;
            if (isWorkbenchNegX && faceType == 1) return frontTile;
            if (isWorkbenchPosZ && faceType == 4) return frontTile;
            if (isWorkbenchNegZ && faceType == 5) return frontTile;
            return sideTile;
        }
        if (isFirTopY) {
            if (faceType == 2) return (side >= 0) ? side : top;
            if (faceType == 3) return (bottom >= 0) ? bottom : side;
            return side;
        }
        if (ProceduralGrassSystemLogic::IsProceduralGrassPrototypeName(proto.name)) {
            return ProceduralGrassSystemLogic::ProceduralGrassTileIndex();
        }
        int resolved = side;
        switch (faceType) {
            case 2: resolved = top; break;
            case 3: resolved = bottom; break;
            default: break;
        }

        const bool isNoGrassVariantPrototype =
            (proto.name == "GrassTuftMiniPineBottom")
            || (proto.name == "GrassTuftMiniPineTop")
            || (proto.name == "GrassTuftMiniPineTripleBottom")
            || (proto.name == "GrassTuftMiniPineTripleMiddle")
            || (proto.name == "GrassTuftMiniPineTripleTop")
            || (proto.name == "GrassTuftLeafFanOak")
            || (proto.name == "GrassTuftLeafFanPine")
            || (proto.name == "GrassTuftFlaxDoubleBottom")
            || (proto.name == "GrassTuftFlaxDoubleTop")
            || (proto.name == "GrassTuftHempDoubleBottom")
            || (proto.name == "GrassTuftHempDoubleTop");
        if (isNoGrassVariantPrototype) {
            return encodeNoGrassVariantTile(resolved);
        }

        return resolved;
    }

    void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers, IRenderBackend& renderBackend) {
        for (RenderHandle& vao : buffers.vaos) {
            renderBackend.destroyVertexArray(vao);
        }
        for (RenderHandle& vbo : buffers.instanceVBOs) {
            renderBackend.destroyArrayBuffer(vbo);
        }
        DestroyVoxelFaceRenderBuffers(buffers.faceBuffers, renderBackend);
        DestroyVoxelWaterRenderBuffers(buffers.waterBuffers, renderBackend);
        buffers.vaos.fill(0);
        buffers.instanceVBOs.fill(0);
        buffers.counts.fill(0);
        buffers.usesTexturedFaceBuffers = false;
        buffers.builtWithFaceCulling = false;
    }

    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stoi(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
    }

    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<bool>(it->second)) return fallback;
        return std::get<bool>(it->second);
    }

    float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stof(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
    }

    namespace {
    }

    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend) {
            std::cerr << "ERROR: RenderSystem cannot init without RendererContext, WorldContext, and render backend." << std::endl;
            return;
        }
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        auto ensureShader = [&](std::unique_ptr<Shader>& shader,
                                const char* vertexKey,
                                const char* fragmentKey,
                                const char* label) {
            const auto vertexIt = world.shaders.find(vertexKey);
            const auto fragmentIt = world.shaders.find(fragmentKey);
            if (vertexIt == world.shaders.end() || fragmentIt == world.shaders.end()) {
                std::cerr << "RenderInitSystem: missing shader source for " << label << std::endl;
                return;
            }
            const char* vertexSource = vertexIt->second.c_str();
            const char* fragmentSource = fragmentIt->second.c_str();
            if (!shader) {
                shader = std::make_unique<Shader>(vertexSource, fragmentSource);
                if (!shader->isValid()) {
                    std::cerr << "RenderInitSystem: failed to create shader for " << label << std::endl;
                    shader.reset();
                }
                return;
            }
            std::string rebuildError;
            if (!shader->rebuild(vertexSource, fragmentSource, &rebuildError)) {
                std::cerr << "RenderInitSystem: failed to rebuild shader for " << label
                          << " (" << rebuildError << ")" << std::endl;
            }
        };

        ensureShader(renderer.blockShader, "BLOCK_VERTEX_SHADER", "BLOCK_FRAGMENT_SHADER", "blockShader");
        ensureShader(renderer.faceShader, "FACE_VERTEX_SHADER", "FACE_FRAGMENT_SHADER", "faceShader");
        ensureShader(renderer.occlusionFaceShader, "OCCLUSION_FACE_VERTEX_SHADER", "OCCLUSION_FACE_FRAGMENT_SHADER", "occlusionFaceShader");
        ensureShader(renderer.waterShader, "WATER_VERTEX_SHADER", "WATER_FRAGMENT_SHADER", "waterShader");
        ensureShader(renderer.waterCompositeShader,
                     "WATER_COMPOSITE_VERTEX_SHADER",
                     "WATER_COMPOSITE_FRAGMENT_SHADER",
                     "waterCompositeShader");
        ensureShader(renderer.skyboxShader, "SKYBOX_VERTEX_SHADER", "SKYBOX_FRAGMENT_SHADER", "skyboxShader");
        ensureShader(renderer.sunMoonShader, "SUNMOON_VERTEX_SHADER", "SUNMOON_FRAGMENT_SHADER", "sunMoonShader");
        ensureShader(renderer.starShader, "STAR_VERTEX_SHADER", "STAR_FRAGMENT_SHADER", "starShader");
        ensureShader(renderer.godrayRadialShader, "GODRAY_VERTEX_SHADER", "GODRAY_RADIAL_FRAGMENT_SHADER", "godrayRadialShader");
        ensureShader(renderer.godrayCompositeShader, "GODRAY_VERTEX_SHADER", "GODRAY_COMPOSITE_FRAGMENT_SHADER", "godrayCompositeShader");
        const std::vector<VertexAttribLayout> kMeshVertexLayout = {
            {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(3 * sizeof(float)), 0u},
            {2u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(6 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kBranchInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, rotation), 1u},
            {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, color), 1u},
        };
        const std::vector<VertexAttribLayout> kBlockInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
            {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
        };
        const std::vector<VertexAttribLayout> kFaceInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, color), 1u},
            {5u, 1, VertexAttribType::Int,   false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, tileIndex), 1u},
            {6u, 1, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, alpha), 1u},
            {7u, 4, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, ao), 1u},
            {8u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, scale), 1u},
            {9u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, uvScale), 1u},
        };
        const std::vector<VertexAttribLayout> kPos2Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(2 * sizeof(float)), 0u, 0u},
        };
        const std::vector<VertexAttribLayout> kPos3Norm3Layout = {
            {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(6 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(6 * sizeof(float)), static_cast<size_t>(3 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kPos2Uv2Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(4 * sizeof(float)), 0u, 0u},
            {1u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(4 * sizeof(float)), static_cast<size_t>(2 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kPos2Color3Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(5 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(5 * sizeof(float)), static_cast<size_t>(2 * sizeof(float)), 0u},
        };
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        renderer.behaviorVAOs.resize(behaviorCount);
        renderer.behaviorInstanceVBOs.resize(behaviorCount);
        renderBackend.ensureArrayBuffer(renderer.cubeVBO);
        renderBackend.uploadArrayBufferData(
            renderer.cubeVBO,
            world.cubeVertices.data(),
            world.cubeVertices.size() * sizeof(float),
            false
        );
        for (int i = 0; i < behaviorCount; ++i) {
            renderBackend.ensureVertexArray(renderer.behaviorVAOs[i]);
            renderBackend.ensureArrayBuffer(renderer.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                renderBackend.configureVertexArray(
                    renderer.behaviorVAOs[i],
                    renderer.cubeVBO,
                    kMeshVertexLayout,
                    renderer.behaviorInstanceVBOs[i],
                    kBranchInstanceLayout
                );
            } else {
                renderBackend.configureVertexArray(
                    renderer.behaviorVAOs[i],
                    renderer.cubeVBO,
                    kMeshVertexLayout,
                    renderer.behaviorInstanceVBOs[i],
                    kBlockInstanceLayout
                );
            }
        }
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        renderBackend.ensureVertexArray(renderer.skyboxVAO);
        renderBackend.ensureArrayBuffer(renderer.skyboxVBO);
        renderBackend.uploadArrayBufferData(renderer.skyboxVBO, skyboxQuadVertices, sizeof(skyboxQuadVertices), false);
        renderBackend.configureVertexArray(renderer.skyboxVAO, renderer.skyboxVBO, kPos2Layout, 0, {});
        renderBackend.ensureVertexArray(renderer.sunMoonVAO);
        renderBackend.ensureArrayBuffer(renderer.sunMoonVBO);
        renderBackend.uploadArrayBufferData(renderer.sunMoonVBO, skyboxQuadVertices, sizeof(skyboxQuadVertices), false);
        renderBackend.configureVertexArray(renderer.sunMoonVAO, renderer.sunMoonVBO, kPos2Layout, 0, {});
        renderBackend.ensureVertexArray(renderer.starVAO);
        renderBackend.ensureArrayBuffer(renderer.starVBO);

        float faceVerts[] = {
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f
        };
        float clippedFaceVerts[] = {
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             1.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  2.0f, 0.0f,
            -0.5f,  1.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 2.0f
        };
        renderBackend.ensureVertexArray(renderer.faceVAO);
        renderBackend.ensureArrayBuffer(renderer.faceVBO);
        renderBackend.ensureArrayBuffer(renderer.faceClippedVBO);
        renderBackend.ensureArrayBuffer(renderer.faceInstanceVBO);
        renderBackend.uploadArrayBufferData(renderer.faceVBO, faceVerts, sizeof(faceVerts), false);
        renderBackend.uploadArrayBufferData(renderer.faceClippedVBO, clippedFaceVerts, sizeof(clippedFaceVerts), false);
        renderBackend.configureVertexArray(
            renderer.faceVAO,
            renderer.faceVBO,
            kMeshVertexLayout,
            renderer.faceInstanceVBO,
            kFaceInstanceLayout
        );

        // Godray quad
        float quadVerts[] = { -1,-1,  1,-1,  -1,1,  -1,1,  1,-1,  1,1 };
        renderBackend.ensureVertexArray(renderer.godrayQuadVAO);
        renderBackend.ensureArrayBuffer(renderer.godrayQuadVBO);
        renderBackend.uploadArrayBufferData(renderer.godrayQuadVBO, quadVerts, sizeof(quadVerts), false);
        renderBackend.configureVertexArray(renderer.godrayQuadVAO, renderer.godrayQuadVBO, kPos2Layout, 0, {});

        // Godray FBOs
        renderer.godrayWidth = baseSystem.app ? baseSystem.app->windowWidth / renderer.godrayDownsample : 960;
        renderer.godrayHeight = baseSystem.app ? baseSystem.app->windowHeight / renderer.godrayDownsample : 540;

        auto setupFBO = [&](RenderHandle& fbo, RenderHandle& tex, int w, int h){
            if (renderBackend.createColorRenderTarget(w, h, fbo, tex)) {
                return;
            }
            std::cerr << "RenderInitSystem: failed to create color render target " << w << "x" << h << '\n';
            fbo = 0;
            tex = 0;
        };

        setupFBO(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex, renderer.godrayWidth, renderer.godrayHeight);
        setupFBO(renderer.godrayBlurFBO, renderer.godrayBlurTex, renderer.godrayWidth, renderer.godrayHeight);

        const bool waterPlanarReflectionsEnabled = getRegistryBool(baseSystem, "WaterPlanarReflectionsEnabled", false);
        if (waterPlanarReflectionsEnabled) {
            renderer.waterReflectionDownsample = std::max(1, getRegistryInt(baseSystem, "WaterPlanarReflectionDownsample", 2));
            renderer.waterReflectionWidth = baseSystem.app
                ? std::max(1, static_cast<int>(baseSystem.app->windowWidth) / renderer.waterReflectionDownsample)
                : 960;
            renderer.waterReflectionHeight = baseSystem.app
                ? std::max(1, static_cast<int>(baseSystem.app->windowHeight) / renderer.waterReflectionDownsample)
                : 540;
            setupFBO(
                renderer.waterReflectionFBO,
                renderer.waterReflectionTex,
                renderer.waterReflectionWidth,
                renderer.waterReflectionHeight
            );
        } else {
            renderer.waterReflectionFBO = 0;
            renderer.waterReflectionTex = 0;
            renderer.waterReflectionWidth = 0;
            renderer.waterReflectionHeight = 0;
        }

        renderer.waterSceneWidth = baseSystem.app
            ? std::max(1, static_cast<int>(baseSystem.app->windowWidth))
            : 1920;
        renderer.waterSceneHeight = baseSystem.app
            ? std::max(1, static_cast<int>(baseSystem.app->windowHeight))
            : 1080;
        setupFBO(
            renderer.waterSceneFBO,
            renderer.waterSceneTex,
            renderer.waterSceneWidth,
            renderer.waterSceneHeight
        );
        setupFBO(
            renderer.waterSceneCopyFBO,
            renderer.waterSceneCopyTex,
            renderer.waterSceneWidth,
            renderer.waterSceneHeight
        );

        ensureShader(renderer.selectionShader, "SELECTION_VERTEX_SHADER", "SELECTION_FRAGMENT_SHADER", "selectionShader");
        std::vector<float> selectionVertices;
        selectionVertices.reserve((12 + 12) * 2 * 6);
        auto pushVertex = [&](const glm::vec3& pos, const glm::vec3& normal){
            selectionVertices.push_back(pos.x);
            selectionVertices.push_back(pos.y);
            selectionVertices.push_back(pos.z);
            selectionVertices.push_back(normal.x);
            selectionVertices.push_back(normal.y);
            selectionVertices.push_back(normal.z);
        };
        auto addLine = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& normal){
            pushVertex(a, normal);
            pushVertex(b, normal);
        };
        auto addFace = [&](const glm::vec3& normal, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d){
            addLine(a, c, normal);
            addLine(b, d, normal);
            addLine(a, b, normal);
            addLine(b, c, normal);
            addLine(c, d, normal);
            addLine(d, a, normal);
        };
        addFace(glm::vec3(0,0,1),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,0,-1),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(1,0,0),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(0.5f,0.5f,-0.5f));
        addFace(glm::vec3(-1,0,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(0,1,0),
                glm::vec3(-0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,-1,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f));

        renderer.selectionVertexCount = static_cast<int>(selectionVertices.size() / 6);
        renderBackend.ensureVertexArray(renderer.selectionVAO);
        renderBackend.ensureArrayBuffer(renderer.selectionVBO);
        renderBackend.uploadArrayBufferData(
            renderer.selectionVBO,
            selectionVertices.data(),
            selectionVertices.size() * sizeof(float),
            false
        );
        renderBackend.configureVertexArray(renderer.selectionVAO, renderer.selectionVBO, kPos3Norm3Layout, 0, {});

        ensureShader(renderer.hudShader, "HUD_VERTEX_SHADER", "HUD_FRAGMENT_SHADER", "hudShader");
        ensureShader(renderer.colorEmotionShader,
                     "COLOR_EMOTION_VERTEX_SHADER",
                     "COLOR_EMOTION_FRAGMENT_SHADER",
                     "colorEmotionShader");
        ensureShader(renderer.crosshairShader, "CROSSHAIR_VERTEX_SHADER", "CROSSHAIR_FRAGMENT_SHADER", "crosshairShader");
        float hudVertices[] = {
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,  -0.05f, 1.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f,  0.05f, 0.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.hudVAO);
        renderBackend.ensureArrayBuffer(renderer.hudVBO);
        renderBackend.uploadArrayBufferData(renderer.hudVBO, hudVertices, sizeof(hudVertices), false);
        renderBackend.configureVertexArray(renderer.hudVAO, renderer.hudVBO, kPos2Uv2Layout, 0, {});

        float colorEmotionVertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.colorEmotionVAO);
        renderBackend.ensureArrayBuffer(renderer.colorEmotionVBO);
        renderBackend.uploadArrayBufferData(renderer.colorEmotionVBO, colorEmotionVertices, sizeof(colorEmotionVertices), false);
        renderBackend.configureVertexArray(renderer.colorEmotionVAO, renderer.colorEmotionVBO, kPos2Uv2Layout, 0, {});

        float chLen = 0.02f;
        float chLenH = 0.016f;
        float crosshairVertices[] = {
            0.0f, -chLen, 1.0f, 1.0f, 1.0f,
            0.0f,  chLen, 1.0f, 1.0f, 1.0f,
            -chLenH, 0.0f, 1.0f, 1.0f, 1.0f,
             chLenH, 0.0f, 1.0f, 1.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.crosshairVAO);
        renderBackend.ensureArrayBuffer(renderer.crosshairVBO);
        renderBackend.uploadArrayBufferData(renderer.crosshairVBO, crosshairVertices, sizeof(crosshairVertices), false);
        renderBackend.configureVertexArray(renderer.crosshairVAO, renderer.crosshairVBO, kPos2Color3Layout, 0, {});
        renderer.crosshairVertexCount = 4;

        renderBackend.ensureVertexArray(renderer.leyLineDebugVAO);
        renderBackend.ensureArrayBuffer(renderer.leyLineDebugVBO);
        renderBackend.uploadArrayBufferData(renderer.leyLineDebugVBO, nullptr, 0, true);
        renderBackend.configureVertexArray(renderer.leyLineDebugVAO, renderer.leyLineDebugVBO, kPos3Norm3Layout, 0, {});
        renderer.leyLineDebugVertexCount = 0;
        renderBackend.unbindVertexArray();
    }


    void CleanupRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.renderBackend) return;
        VoxelMeshingSystemLogic::ResetMeshingRuntime();
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        for (RenderHandle& vao : renderer.behaviorVAOs) {
            renderBackend.destroyVertexArray(vao);
        }
        for (RenderHandle& vbo : renderer.behaviorInstanceVBOs) {
            renderBackend.destroyArrayBuffer(vbo);
        }
        renderBackend.destroyVertexArray(renderer.skyboxVAO);
        renderBackend.destroyVertexArray(renderer.sunMoonVAO);
        renderBackend.destroyVertexArray(renderer.starVAO);
        renderBackend.destroyArrayBuffer(renderer.cubeVBO);
        renderBackend.destroyArrayBuffer(renderer.skyboxVBO);
        renderBackend.destroyArrayBuffer(renderer.sunMoonVBO);
        renderBackend.destroyArrayBuffer(renderer.starVBO);
        renderBackend.destroyVertexArray(renderer.selectionVAO);
        renderBackend.destroyArrayBuffer(renderer.selectionVBO);
        renderBackend.destroyVertexArray(renderer.hudVAO);
        renderBackend.destroyArrayBuffer(renderer.hudVBO);
        renderBackend.destroyVertexArray(renderer.colorEmotionVAO);
        renderBackend.destroyArrayBuffer(renderer.colorEmotionVBO);
        renderBackend.destroyVertexArray(renderer.crosshairVAO);
        renderBackend.destroyArrayBuffer(renderer.crosshairVBO);
        renderBackend.destroyVertexArray(renderer.uiVAO);
        renderBackend.destroyArrayBuffer(renderer.uiVBO);
        renderBackend.destroyVertexArray(renderer.uiButtonVAO);
        renderBackend.destroyArrayBuffer(renderer.uiButtonVBO);
        renderBackend.destroyVertexArray(renderer.fontVAO);
        renderBackend.destroyArrayBuffer(renderer.fontVBO);
        renderBackend.destroyVertexArray(renderer.audioRayVAO);
        renderBackend.destroyArrayBuffer(renderer.audioRayVBO);
        renderBackend.destroyVertexArray(renderer.leyLineDebugVAO);
        renderBackend.destroyArrayBuffer(renderer.leyLineDebugVBO);
        renderBackend.destroyVertexArray(renderer.fishingVAO);
        renderBackend.destroyArrayBuffer(renderer.fishingVBO);
        renderBackend.destroyVertexArray(renderer.gemVAO);
        renderBackend.destroyArrayBuffer(renderer.gemVBO);
        renderBackend.destroyVertexArray(renderer.audioRayVoxelVAO);
        renderBackend.destroyArrayBuffer(renderer.audioRayVoxelInstanceVBO);
        renderBackend.destroyVertexArray(renderer.faceVAO);
        renderBackend.destroyArrayBuffer(renderer.faceVBO);
        renderBackend.destroyArrayBuffer(renderer.faceClippedVBO);
        renderBackend.destroyArrayBuffer(renderer.faceInstanceVBO);
        renderBackend.destroyVertexArray(renderer.grass3DVolumeVAO);
        renderBackend.destroyArrayBuffer(renderer.grass3DVolumeVBO);
        renderBackend.destroyArrayBuffer(renderer.grass3DVolumeInstanceVBO);
        renderer.grass3DVolumeVertexCount = 0;
        auto destroyTexture = [&](RenderHandle& texture) {
            if (!texture) return;
            renderBackend.destroyTexture(texture);
        };
        destroyTexture(renderer.atlasTexture);
        for (RenderHandle& tex : renderer.grassTextures) {
            destroyTexture(tex);
        }
        renderer.grassTextureCount = 0;
        for (RenderHandle& tex : renderer.shortGrassTextures) {
            destroyTexture(tex);
        }
        renderer.shortGrassTextureCount = 0;
        for (RenderHandle& tex : renderer.oreTextures) {
            destroyTexture(tex);
        }
        renderer.oreTextureCount = 0;
        for (RenderHandle& tex : renderer.terrainTextures) {
            destroyTexture(tex);
        }
        renderer.terrainTextureCount = 0;
        destroyTexture(renderer.waterOverlayTexture);
        destroyTexture(renderer.proceduralGrassTexture);
        renderer.proceduralGrassTextureSize = glm::ivec2(0);
        renderBackend.destroyColorRenderTarget(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex);
        renderBackend.destroyColorRenderTarget(renderer.occlusionHzbFBO, renderer.occlusionHzbTex);
        renderBackend.destroyColorRenderTarget(renderer.godrayBlurFBO, renderer.godrayBlurTex);
        renderBackend.destroyColorRenderTarget(renderer.waterReflectionFBO, renderer.waterReflectionTex);
        renderBackend.destroyColorRenderTarget(renderer.waterSceneFBO, renderer.waterSceneTex);
        renderBackend.destroyColorRenderTarget(renderer.waterSceneCopyFBO, renderer.waterSceneCopyTex);
        renderer.waterReflectionWidth = 0;
        renderer.waterReflectionHeight = 0;
        renderer.waterSceneWidth = 0;
        renderer.waterSceneHeight = 0;
        renderer.occlusionHzbWidth = 0;
        renderer.occlusionHzbHeight = 0;

        renderer.blockShader.reset();
        renderer.skyboxShader.reset();
        renderer.sunMoonShader.reset();
        renderer.starShader.reset();
        renderer.selectionShader.reset();
        renderer.hudShader.reset();
        renderer.crosshairShader.reset();
        renderer.colorEmotionShader.reset();
        renderer.faceShader.reset();
        renderer.occlusionFaceShader.reset();
        renderer.waterShader.reset();
        renderer.waterCompositeShader.reset();
        renderer.grass3DShader.reset();
        renderer.fontShader.reset();
        renderer.uiShader.reset();
        renderer.uiColorShader.reset();
        renderer.glyphShader.reset();
        renderer.audioRayShader.reset();
        renderer.audioRayVoxelShader.reset();
        renderer.godrayRadialShader.reset();
        renderer.godrayCompositeShader.reset();
        renderer.cloudShader.reset();
        renderer.auroraShader.reset();
    }
}
