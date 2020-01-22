// Copyright (C) 2019-2020 star.engine at outlook dot com
//
// This file is part of StarEngine
//
// StarEngine is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// StarEngine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with StarEngine.  If not, see <https://www.gnu.org/licenses/>.

#include "SShaderAssetBuilder.h"
#include "SShaderCompiler.h"
#include <StarCompiler/ShaderGraph/SShaderGroups.h>
#include <StarCompiler/ShaderWorks/SHLSLGenerator.h>
#include <StarCompiler/ShaderGraph/SShaderState.h>
#include <StarCompiler/STextUtils.h>

namespace Star::Graphics::Render::Shader {

void ShaderAssetBuilder::buildShaders(const ShaderDatabase& database, const ShaderGroups& sw, const ShaderModules& modules) {
    for (const auto& [prototypeName, prototype] : database.mPrototypes) {
        auto res = mShaders.emplace(std::piecewise_construct,
            std::forward_as_tuple(prototypeName), std::forward_as_tuple());
        Ensures(res.second);

        auto& prototypeData = res.first->second;
        for (const auto& [bundleName, bundle] : prototype.mSolutions) {
            auto res = prototypeData.mSolutions.emplace(std::piecewise_construct,
                std::forward_as_tuple(bundleName), std::forward_as_tuple());
            Ensures(res.second);

            auto& bundleData = res.first->second;
            for (const auto& [pipelineName, pipeline] : bundle.mPipelines) {
                auto res = bundleData.mPipelines.emplace(std::piecewise_construct,
                    std::forward_as_tuple(pipelineName), std::forward_as_tuple());
                Ensures(res.second);

                auto& pipelineData = res.first->second;
                for (const auto& [queueName, queue] : pipeline.mQueues) {
                    auto res = pipelineData.mQueues.emplace(std::piecewise_construct,
                        std::forward_as_tuple(queueName), std::forward_as_tuple());
                    Ensures(res.second);

                    // get group
                    const auto& group = sw.getGroup(bundleName, pipelineName, queueName);

                    auto& queueData = res.first->second;
                    queueData.mLevels.reserve(queue.mLevels.size());
                    for (const auto& level : queue.mLevels) {
                        queueData.mLevels.emplace_back();
                        auto& levelData = queueData.mLevels.back();

                        for (const auto& [passName, pass] : level.mPasses) {
                            auto res = levelData.mPasses.emplace(std::piecewise_construct,
                                std::forward_as_tuple(passName), std::forward_as_tuple());
                            Ensures(res.second);

                            auto& passData = res.first->second;
                            passData.mSubpasses.reserve(pass.size());
                            size_t k = 0;
                            for (const auto& subpass : pass) {
                                auto shaderName = std::to_string(k++) + "/";
                                if (passName.empty())
                                    shaderName += "@";
                                else
                                    shaderName += passName;
                                shaderName = shaderName + "/" + prototypeName;

                                const auto& [pProgram, rsg] = group.mPrograms.at(shaderName);
                                HLSLGenerator hlsl(*pProgram);

                                passData.mSubpasses.emplace_back();
                                auto& subpassData = passData.mSubpasses.back();

                                subpassData.mState.mStreamOutput = {};
                                subpassData.mState.mBlendState = getRenderType(subpass.mShaderState.mBlendState);
                                subpassData.mState.mSampleMask = subpass.mShaderState.mSampleMask;
                                subpassData.mState.mRasterizerState = getRenderType(subpass.mShaderState.mRasterizerState);
                                subpassData.mState.mDepthStencilState = getRenderType(subpass.mShaderState.mDepthStencilState);

                                for (const auto& [stageID, stage] : subpass.mProgram.mShaders) {
                                    std::ostringstream oss;
                                    std::string space;
                                    oss << hlsl.generateAttributes(modules.mAttributes, stageID, group.getRootSignatureShaderGroup(), rsg);
                                    if (!oss.str().empty())
                                        oss << "\n";
                                    oss << hlsl.generateModules();
                                    oss << hlsl.generateShader(stageID);

                                    visit(overload(
                                        [&](PS_) {
                                            compileShader(subpassData.mProgram.mPS, "ps_5_0",
                                                shaderName + ".ps", oss.str());
                                        },
                                        [&](GS_) {
                                            throw std::invalid_argument("shader not supported");
                                        },
                                        [&](DS_) {
                                            throw std::invalid_argument("shader not supported");
                                        },
                                        [&](HS_) {
                                            throw std::invalid_argument("shader not supported");
                                        },
                                        [&](VS_) {
                                            auto inputs = hlsl.mProgram.mShaders.at(VS).mGraph.getNodeInputsWithoutSource();
                                            for (const auto& [name, input] : inputs) {
                                                bool isVertexType = true;
                                                VertexElementType type;
                                                visit(overload(
                                                    [&](BINORMAL_ s) { type = s; },
                                                    [&](BLENDINDICES_ s) { type = s; },
                                                    [&](BLENDWEIGHT_ s) { type = s; },
                                                    [&](NORMAL_ s) { type = s; },
                                                    [&](POSITIONT_ s) { type = s; },
                                                    [&](PSIZE_ s) { type = s; },
                                                    [&](TANGENT_ s) { type = s; },
                                                    [&](TEXCOORD_ s) { type = s; },
                                                    [&](SV_Position_ s) { type = s; },
                                                    [&](auto) { isVertexType = false; }
                                                ), input.mSemantic);
                                                if (isVertexType) {
                                                    subpassData.mInputLayout.mSemantics[type].emplace_back(input.mName);
                                                }
                                            }
                                            compileShader(subpassData.mProgram.mVS, "vs_5_0",
                                                shaderName + ".vs", oss.str());
                                        },
                                        [](auto) {
                                            throw std::runtime_error("unknown shader");
                                        }
                                    ), stageID);
                                }
                                ++k;
                            } // subpass
                        } // pass
                    } // level
                } // queue
            } // pipeline
        } // bundle
    } // prototype
}

void ShaderAssetBuilder::build(Resources& resources) const {
    boost::uuids::name_generator_sha1 gen(boost::uuids::ns::oid());
    
    resources.mShaders.reserve(mShaders.size());
    for (const auto& [prototypeName, prototypeData] : mShaders) {
        auto metaID = gen(prototypeName);
        resources.mShaders.emplace(std::piecewise_construct,
            std::forward_as_tuple(metaID), std::forward_as_tuple(prototypeData));
    }
}

void buildShaderData(const ShaderPrototype& prototype, const ShaderModules& modules,
    const ShaderGroups& sw, ShaderData& prototypeData, bool bCompile
) {
    prototypeData.mName = prototype.mName;
    for (const auto& [bundleName, bundle] : prototype.mSolutions) {
        auto res = prototypeData.mSolutions.emplace(std::piecewise_construct,
            std::forward_as_tuple(bundleName), std::forward_as_tuple());
        Ensures(res.second);

        auto& bundleData = res.first->second;
        for (const auto& [pipelineName, pipeline] : bundle.mPipelines) {
            auto res = bundleData.mPipelines.emplace(std::piecewise_construct,
                std::forward_as_tuple(pipelineName), std::forward_as_tuple());
            Ensures(res.second);

            auto& pipelineData = res.first->second;
            for (const auto& [queueName, queue] : pipeline.mQueues) {
                auto res = pipelineData.mQueues.emplace(std::piecewise_construct,
                    std::forward_as_tuple(queueName), std::forward_as_tuple());
                Ensures(res.second);

                // get group
                const auto& group = sw.getGroup(bundleName, pipelineName, queueName);

                auto& queueData = res.first->second;
                queueData.mLevels.reserve(queue.mLevels.size());
                for (const auto& level : queue.mLevels) {
                    queueData.mLevels.emplace_back();
                    auto& levelData = queueData.mLevels.back();

                    for (const auto& [passName, pass] : level.mPasses) {
                        auto res = levelData.mPasses.emplace(std::piecewise_construct,
                            std::forward_as_tuple(passName), std::forward_as_tuple());
                        Ensures(res.second);

                        auto& passData = res.first->second;
                        passData.mSubpasses.reserve(pass.size());
                        size_t k = 0;
                        for (const auto& subpass : pass) {
                            auto shaderName = std::to_string(k++) + "/";
                            if (passName.empty())
                                shaderName += "@";
                            else
                                shaderName += passName;
                            shaderName = shaderName + "/" + prototype.mName;

                            const auto& [pProgram, rsg] = group.mPrograms.at(shaderName);
                            HLSLGenerator hlsl(*pProgram);

                            passData.mSubpasses.emplace_back();
                            auto& subpassData = passData.mSubpasses.back();

                            subpassData.mState.mStreamOutput = {};
                            subpassData.mState.mBlendState = getRenderType(subpass.mShaderState.mBlendState);
                            subpassData.mState.mSampleMask = subpass.mShaderState.mSampleMask;
                            subpassData.mState.mRasterizerState = getRenderType(subpass.mShaderState.mRasterizerState);
                            subpassData.mState.mDepthStencilState = getRenderType(subpass.mShaderState.mDepthStencilState);

                            for (const auto& table : rsg.mTables) {
                                if (table.first.mUpdate != UpdateEnum::PerBatch) {
                                    continue;
                                }
                                visit(overload(
                                    [](Constants_) {
                                        throw std::runtime_error("root constants not supported yet");
                                    },
                                    [](CBV_) {
                                        throw std::runtime_error("root cbv not supported yet");
                                    },
                                    [](UAV_) {
                                        throw std::runtime_error("root uav not supported yet");
                                    },
                                    [](SRV_) {
                                        throw std::runtime_error("root srv not supported yet");
                                    },
                                    [&](Table_) {
                                        for (const auto& d : table.second.mDescriptors) {
                                            if (std::holds_alternative<SRV_>(d.mType)) {
                                                Expects(!d.mName.empty());
                                                subpassData.mTextures.emplace_back(d.mName);
                                            }
                                        }
                                    },
                                    [](SSV_) {
                                        // do nothing
                                    }
                                ), table.first.mType);
                            }

                            for (const auto& [stageID, stage] : subpass.mProgram.mShaders) {
                                std::ostringstream oss;
                                std::string space;
                                oss << hlsl.generateAttributes(modules.mAttributes, stageID, group.getRootSignatureShaderGroup(), rsg);
                                if (!oss.str().empty())
                                    oss << "\n";
                                oss << hlsl.generateModules();
                                oss << hlsl.generateShader(stageID);

                                visit(overload(
                                    [&](PS_) {
                                        if (bCompile) {
                                            compileShader(subpassData.mProgram.mPS, "ps_5_0",
                                                shaderName + ".vs", oss.str());
                                        } else {
                                            Expects(subpassData.mProgram.mPS.empty());
                                            auto content = oss.str();
                                            subpassData.mProgram.mPS.insert(
                                                subpassData.mProgram.mPS.end(),
                                                content.begin(), content.end());
                                        }
                                    },
                                    [&](GS_) {
                                        throw std::invalid_argument("shader not supported");
                                    },
                                    [&](DS_) {
                                        throw std::invalid_argument("shader not supported");
                                    },
                                    [&](HS_) {
                                        throw std::invalid_argument("shader not supported");
                                    },
                                    [&](VS_) {
                                        auto inputs = hlsl.mProgram.mShaders.at(VS).mGraph.getNodeInputsWithoutSource();
                                        for (const auto& [name, input] : inputs) {
                                            bool isVertexType = true;
                                            VertexElementType type;
                                            visit(overload(
                                                [&](BINORMAL_ s) { type = s; },
                                                [&](BLENDINDICES_ s) { type = s; },
                                                [&](BLENDWEIGHT_ s) { type = s; },
                                                [&](NORMAL_ s) { type = s; },
                                                [&](POSITIONT_ s) { type = s; },
                                                [&](PSIZE_ s) { type = s; },
                                                [&](TANGENT_ s) { type = s; },
                                                [&](TEXCOORD_ s) { type = s; },
                                                [&](SV_Position_ s) { type = s; },
                                                [&](auto) { isVertexType = false; }
                                            ), input.mSemantic);
                                            if (isVertexType) {
                                                subpassData.mInputLayout.mSemantics[type].emplace_back(input.mName);
                                            }
                                        }
                                        if (bCompile) {
                                            compileShader(subpassData.mProgram.mVS, "vs_5_0",
                                                shaderName + ".vs", oss.str());
                                        } else {
                                            Expects(subpassData.mProgram.mVS.empty());
                                            auto content = oss.str();
                                            subpassData.mProgram.mVS.insert(
                                                subpassData.mProgram.mVS.end(),
                                                content.begin(), content.end());
                                        }
                                    },
                                    [](auto) {
                                        throw std::runtime_error("unknown shader");
                                    }
                                ), stageID);
                            }
                            ++k;
                        } // subpass
                    } // pass
                } // level
            } // queue
        } // pipeline
    } // bundle
}

void buildShaderText(std::ostream& oss, const ShaderData& prototype) {
    std::string space;
    OSS << "Shader \"" << prototype.mName << "\" {\n";
    for (const auto& solution : prototype.mSolutions) {
        oss << "\n";
        OSS << "Solution \"" << solution.first << "\" {\n";
        for (const auto& pipeline : solution.second.mPipelines) {
            oss << "\n";
            OSS << "Pipeline \"" << pipeline.first << "\" {\n";
            for (const auto& queue : pipeline.second.mQueues) {
                oss << "\n";
                OSS << "Queue \"" << queue.first << "\" {\n";
                size_t levelID = 0;
                for (const auto& level : queue.second.mLevels) {
                    oss << "\n";
                    OSS << "Level " << levelID++ << " {\n";
                    for (const auto& var : level.mPasses) {
                        oss << "\n";
                        OSS << "Variant \"" << var.first << "\" {\n";
                        size_t passID = 0;
                        for (const auto& pass : var.second.mSubpasses) {
                            oss << "\n";
                            OSS << "Pass " << passID++ << " {\n";
                            if (!pass.mProgram.mPS.empty()) {
                                oss << "\n";
                                OSS << "PixelShader {\n";
                                {
                                    INDENT();
                                    copyString(oss, space, pass.mProgram.mPS);
                                }
                                OSS << "} // PixelShader\n";
                            }
                            if (!pass.mProgram.mVS.empty()) {
                                oss << "\n";
                                OSS << "VertexShader {\n";
                                {
                                    INDENT();
                                    copyString(oss, space, pass.mProgram.mVS);
                                }
                                OSS << "} // VertexShader\n";
                            }
                            oss << "\n";
                            OSS << "} // Pass\n";
                        }
                        oss << "\n";
                        OSS << "} // Variant\n";
                    }
                    oss << "\n";
                    OSS << "} // Level\n";
                }
                oss << "\n";
                OSS << "} // Queue\n";
            }
            oss << "\n";
            OSS << "} // Pipeline\n";
        }
        oss << "\n";
        OSS << "} // Solution\n";
    }
    oss << "\n";
    OSS << "} // Prototype\n";
}

void buildShaderText2(std::ostream& oss, const ShaderData& prototype) {
    std::string space;
    OSS << "Shader \"" << prototype.mName << "\" {\n";
    for (const auto& solution : prototype.mSolutions) {
        for (const auto& pipeline : solution.second.mPipelines) {
            for (const auto& queue : pipeline.second.mQueues) {
                size_t levelID = 0;
                for (const auto& level : queue.second.mLevels) {
                    for (const auto& var : level.mPasses) {
                        size_t passID = 0;
                        for (const auto& pass : var.second.mSubpasses) {
                            oss << "\n";
                            std::string name;
                            {
                                std::ostringstream oss2;
                                oss2 << "Solution \"" << solution.first << "\" ";
                                oss2 << "Pipeline \"" << pipeline.first << "\" ";
                                oss2 << "Queue \"" << queue.first << "\" ";
                                oss2 << "Level " << levelID++ << " ";
                                oss2 << "Variant \"" << var.first << "\" ";
                                oss2 << "Pass " << passID++;
                                name = oss2.str();
                            }
                            OSS << name << " {\n";
                            if (!pass.mProgram.mVS.empty()) {
                                oss << "\n";
                                OSS << "VertexShader {\n";
                                {
                                    INDENT();
                                    copyString(oss, space, pass.mProgram.mVS);
                                }
                                OSS << "} // VertexShader\n";
                            }

                            if (!pass.mProgram.mPS.empty()) {
                                oss << "\n";
                                OSS << "PixelShader {\n";
                                {
                                    INDENT();
                                    copyString(oss, space, pass.mProgram.mPS);
                                }
                                OSS << "} // PixelShader\n";
                            }
                            oss << "\n";
                            OSS << "} // " << name << "\n";
                            ++passID;
                        }
                    }
                    ++levelID;
                }
            }
        }
    }
    oss << "\n";
    OSS << "} // Shader\n";
}

}