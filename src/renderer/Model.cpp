#include "renderer/Model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <json/json.h>

using json = nlohmann::json;

namespace renderer
{
	namespace
	{
		struct WeldKey
		{
			int64_t x;
			int64_t y;
			int64_t z;

			bool operator==(const WeldKey& other) const
			{
				return x == other.x && y == other.y && z == other.z;
			}
		};

		struct WeldKeyHash
		{
			size_t operator()(const WeldKey& key) const
			{
				uint64_t h = static_cast<uint64_t>(key.x) * 73856093ull;
				h ^= static_cast<uint64_t>(key.y) * 19349663ull;
				h ^= static_cast<uint64_t>(key.z) * 83492791ull;
				return static_cast<size_t>(h);
			}
		};

		WeldKey makeWeldKey(const glm::vec3& p, float invCellSize)
		{
			return WeldKey{
				static_cast<int64_t>(std::llround(p.x * invCellSize)),
				static_cast<int64_t>(std::llround(p.y * invCellSize)),
				static_cast<int64_t>(std::llround(p.z * invCellSize))
			};
		}

		std::vector<uint8_t> decodeBase64(const std::string& encoded)
		{
			static const int kLookup[256] = {
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
				52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
				-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
				15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
				-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
				41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
			};

			std::vector<uint8_t> out;
			out.reserve((encoded.size() * 3) / 4);
			uint32_t val = 0;
			int bits = -8;
			for (unsigned char c : encoded)
			{
				int d = kLookup[c];
				if (d == -1)
				{
					continue;
				}
				val = (val << 6) + static_cast<uint32_t>(d);
				bits += 6;
				if (bits >= 0)
				{
					out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
					bits -= 8;
				}
			}
			return out;
		}

		bool loadGltfBuffers(
			const json& root,
			const std::filesystem::path& baseDir,
			const std::vector<uint8_t>& glbBinChunk,
			std::vector<std::vector<uint8_t>>& buffers)
		{
			if (!root.contains("buffers") || !root["buffers"].is_array())
			{
				return false;
			}

			buffers.clear();
			buffers.reserve(root["buffers"].size());

			for (const auto& buf : root["buffers"])
			{
				if (!buf.contains("uri"))
				{
					if (!glbBinChunk.empty())
					{
						buffers.push_back(glbBinChunk);
						continue;
					}
					return false;
				}
				const std::string uri = buf["uri"].get<std::string>();
				const std::string base64Tag = ";base64,";
				size_t tagPos = uri.find(base64Tag);
				if (uri.rfind("data:", 0) == 0 && tagPos != std::string::npos)
				{
					buffers.push_back(decodeBase64(uri.substr(tagPos + base64Tag.size())));
					continue;
				}

				std::filesystem::path binPath = baseDir / uri;
				std::ifstream binFile(binPath, std::ios::binary | std::ios::ate);
				if (!binFile.is_open())
				{
					std::cerr << "Failed to open glTF binary buffer: " << binPath << std::endl;
					return false;
				}
				std::streamsize binSize = binFile.tellg();
				binFile.seekg(0, std::ios::beg);
				std::vector<uint8_t> data(static_cast<size_t>(binSize));
				binFile.read(reinterpret_cast<char*>(data.data()), binSize);
				buffers.push_back(std::move(data));
			}

			return !buffers.empty();
		}

		template <int N>
		std::vector<std::array<float, N>> readAccessorFloatN(
			const json& root,
			int accessorIndex,
			const std::vector<std::vector<uint8_t>>& buffers)
		{
			std::vector<std::array<float, N>> result;
			const json& accessor = root["accessors"][accessorIndex];
			const size_t count = accessor["count"].get<size_t>();
			const size_t accOffset = accessor.value("byteOffset", size_t(0));
			const int componentType = accessor.value("componentType", 5126);

			if (!accessor.contains("bufferView"))
			{
				result.assign(count, std::array<float, N>{});
				return result;
			}

			const size_t bvIndex = accessor["bufferView"].get<size_t>();
			const json& bufferView = root["bufferViews"][bvIndex];
			const size_t bufferIdx = bufferView.value("buffer", size_t(0));
			const size_t bvOffset = bufferView.value("byteOffset", size_t(0));
			const size_t defaultStride = (componentType == 5126) ? (N * sizeof(float))
				: (componentType == 5123 ? (N * sizeof(uint16_t)) : (N * sizeof(uint8_t)));
			const size_t stride = bufferView.value("byteStride", size_t(0)) > 0
				? bufferView["byteStride"].get<size_t>()
				: defaultStride;

			const auto& bytes = buffers[bufferIdx];
			const size_t start = bvOffset + accOffset;
			result.resize(count);

			for (size_t i = 0; i < count; ++i)
			{
				const size_t elemOffset = start + i * stride;
				if (elemOffset + defaultStride > bytes.size())
				{
					break;
				}
				if (componentType == 5126)
				{
					std::memcpy(result[i].data(), bytes.data() + elemOffset, N * sizeof(float));
				}
				else if (componentType == 5123)
				{
					uint16_t raw[N]{};
					std::memcpy(raw, bytes.data() + elemOffset, N * sizeof(uint16_t));
					for (int c = 0; c < N; ++c)
					{
						result[i][c] = static_cast<float>(raw[c]) / 65535.0f;
					}
				}
				else if (componentType == 5121)
				{
					for (int c = 0; c < N; ++c)
					{
						result[i][c] = static_cast<float>(bytes[elemOffset + c]) / 255.0f;
					}
				}
			}

			return result;
		}

		std::vector<GLuint> readAccessorIndices(
			const json& root,
			int accessorIndex,
			const std::vector<std::vector<uint8_t>>& buffers)
		{
			const json& accessor = root["accessors"][accessorIndex];
			const size_t count = accessor["count"].get<size_t>();
			const size_t accOffset = accessor.value("byteOffset", size_t(0));
			const int componentType = accessor["componentType"].get<int>();
			const size_t bvIndex = accessor["bufferView"].get<size_t>();
			const json& bufferView = root["bufferViews"][bvIndex];
			const size_t bufferIdx = bufferView.value("buffer", size_t(0));
			const size_t bvOffset = bufferView.value("byteOffset", size_t(0));

			const auto& bytes = buffers[bufferIdx];
			const size_t start = bvOffset + accOffset;
			std::vector<GLuint> indices;
			indices.reserve(count);

			if (componentType == 5125) // GL_UNSIGNED_INT
			{
				const auto* ptr = reinterpret_cast<const uint32_t*>(bytes.data() + start);
				for (size_t i = 0; i < count; ++i)
				{
					indices.push_back(ptr[i]);
				}
			}
			else if (componentType == 5123) // GL_UNSIGNED_SHORT
			{
				const auto* ptr = reinterpret_cast<const uint16_t*>(bytes.data() + start);
				for (size_t i = 0; i < count; ++i)
				{
					indices.push_back(ptr[i]);
				}
			}
			else if (componentType == 5122) // GL_SHORT
			{
				const auto* ptr = reinterpret_cast<const int16_t*>(bytes.data() + start);
				for (size_t i = 0; i < count; ++i)
				{
					indices.push_back(static_cast<GLuint>(ptr[i]));
				}
			}
			else if (componentType == 5121) // GL_UNSIGNED_BYTE
			{
				const uint8_t* ptr = bytes.data() + start;
				for (size_t i = 0; i < count; ++i)
				{
					indices.push_back(ptr[i]);
				}
			}

			return indices;
		}

		glm::mat4 computeNodeLocalMatrix(const json& node)
		{
			if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
			{
				float m[16];
				for (int i = 0; i < 16; ++i)
				{
					m[i] = node["matrix"][i].get<float>();
				}
				return glm::make_mat4(m);
			}

			glm::mat4 mat(1.0f);
			if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() == 3)
			{
				glm::vec3 t(
					node["translation"][0].get<float>(),
					node["translation"][1].get<float>(),
					node["translation"][2].get<float>());
				mat = glm::translate(mat, t);
			}
			if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4)
			{
				// glTF stores quaternion as [x, y, z, w]. glm::quat takes (w, x, y, z).
				glm::quat q(
					node["rotation"][3].get<float>(),
					node["rotation"][0].get<float>(),
					node["rotation"][1].get<float>(),
					node["rotation"][2].get<float>());
				mat *= glm::mat4_cast(glm::normalize(q));
			}
			if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() == 3)
			{
				glm::vec3 s(
					node["scale"][0].get<float>(),
					node["scale"][1].get<float>(),
					node["scale"][2].get<float>());
				mat = glm::scale(mat, s);
			}
			return mat;
		}

		float cornerAngle(const glm::vec3& edgeA, const glm::vec3& edgeB)
		{
			float lenA = glm::length(edgeA);
			float lenB = glm::length(edgeB);
			if (lenA < 1e-8f || lenB < 1e-8f)
			{
				return 0.0f;
			}
			float cosTheta = glm::clamp(glm::dot(edgeA / lenA, edgeB / lenB), -1.0f, 1.0f);
			return std::acos(cosTheta);
		}

		void appendPrimitiveGeometry(
			const json& root,
			const json& primitive,
			const std::vector<std::vector<uint8_t>>& buffers,
			const glm::mat4& worldMatrix,
			std::vector<Vertex>& outVertices,
			std::vector<GLuint>& outIndices,
			std::vector<GpuTriangle>& outTriangles,
			glm::vec3& aabbMin,
			glm::vec3& aabbMax)
		{
			const int mode = primitive.value("mode", 4);
			if (mode != 4 || !primitive.contains("attributes"))
			{
				return;
			}

			const auto& attrs = primitive["attributes"];
			if (!attrs.contains("POSITION"))
			{
				return;
			}

			const auto posData = readAccessorFloatN<3>(root, attrs["POSITION"].get<int>(), buffers);
			const size_t vertexCount = posData.size();
			if (vertexCount == 0)
			{
				return;
			}

			std::vector<std::array<float, 3>> normData;
			const bool hasNormals = attrs.contains("NORMAL");
			if (hasNormals)
			{
				normData = readAccessorFloatN<3>(root, attrs["NORMAL"].get<int>(), buffers);
			}

			std::vector<std::array<float, 2>> uvData;
			const bool hasUvs = attrs.contains("TEXCOORD_0");
			if (hasUvs)
			{
				uvData = readAccessorFloatN<2>(root, attrs["TEXCOORD_0"].get<int>(), buffers);
			}

			std::vector<GLuint> localIndices;
			if (primitive.contains("indices"))
			{
				localIndices = readAccessorIndices(root, primitive["indices"].get<int>(), buffers);
			}
			else
			{
				localIndices.resize(vertexCount);
				for (size_t i = 0; i < vertexCount; ++i)
				{
					localIndices[i] = static_cast<GLuint>(i);
				}
			}

			if (localIndices.size() < 3)
			{
				return;
			}

			const glm::mat3 linearMat(worldMatrix);
			const glm::mat3 normalMat = glm::transpose(glm::inverse(linearMat));
			const bool flipWinding = glm::determinant(linearMat) < 0.0f;
			const GLuint baseVertex = static_cast<GLuint>(outVertices.size());

			bool isOpenSheet = false;
			if (primitive.contains("material") && root.contains("materials") && root["materials"].is_array())
			{
				const int matIdx = primitive["material"].get<int>();
				if (matIdx >= 0 && matIdx < static_cast<int>(root["materials"].size()))
				{
					const json& mat = root["materials"][matIdx];
					const std::string alphaMode = mat.value("alphaMode", std::string("OPAQUE"));
					if (mat.value("doubleSided", false) || alphaMode == "MASK" || alphaMode == "BLEND")
					{
						isOpenSheet = true;
					}
				}
			}
			const float sheetFlag = isOpenSheet ? 1.0f : 0.0f;

			outVertices.reserve(outVertices.size() + vertexCount);
			for (size_t i = 0; i < vertexCount; ++i)
			{
				Vertex v{};
				glm::vec4 worldPos = worldMatrix * glm::vec4(posData[i][0], posData[i][1], posData[i][2], 1.0f);
				v.position = glm::vec3(worldPos);
				aabbMin = glm::min(aabbMin, v.position);
				aabbMax = glm::max(aabbMax, v.position);

				if (hasNormals && i < normData.size())
				{
					glm::vec3 n = normalMat * glm::vec3(normData[i][0], normData[i][1], normData[i][2]);
					float nLen = glm::length(n);
					v.normal = (nLen > 1e-6f) ? (n / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);
				}
				else
				{
					v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
				}

				v.color = glm::vec3(1.0f, 1.0f, sheetFlag);
				v.texUV = (hasUvs && i < uvData.size())
					? glm::vec2(uvData[i][0], uvData[i][1])
					: glm::vec2(0.0f);
				outVertices.push_back(v);
			}

			const size_t triCount = localIndices.size() / 3;
			outIndices.reserve(outIndices.size() + triCount * 3);
			outTriangles.reserve(outTriangles.size() + triCount);

			for (size_t t = 0; t < triCount; ++t)
			{
				GLuint i0 = localIndices[t * 3 + 0];
				GLuint i1 = localIndices[t * 3 + (flipWinding ? 2 : 1)];
				GLuint i2 = localIndices[t * 3 + (flipWinding ? 1 : 2)];
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
				{
					continue;
				}

				outIndices.push_back(baseVertex + i0);
				outIndices.push_back(baseVertex + i1);
				outIndices.push_back(baseVertex + i2);

				const glm::vec3& p0 = outVertices[baseVertex + i0].position;
				const glm::vec3& p1 = outVertices[baseVertex + i1].position;
				const glm::vec3& p2 = outVertices[baseVertex + i2].position;
				glm::vec3 crossN = glm::cross(p1 - p0, p2 - p0);
				float area2 = glm::length(crossN);
				if (area2 < 1e-12f)
				{
					continue;
				}

				glm::vec3 faceNorm = crossN / area2;
				if (!hasNormals)
				{
					outVertices[baseVertex + i0].normal = faceNorm;
					outVertices[baseVertex + i1].normal = faceNorm;
					outVertices[baseVertex + i2].normal = faceNorm;
				}

				GpuTriangle tri{};
				tri.v0 = glm::vec4(p0, 0.0f);
				tri.v1 = glm::vec4(p1, 0.0f);
				tri.v2 = glm::vec4(p2, 0.0f);
				tri.n0 = glm::vec4(outVertices[baseVertex + i0].normal, sheetFlag);
				tri.n1 = glm::vec4(outVertices[baseVertex + i1].normal, sheetFlag);
				tri.n2 = glm::vec4(outVertices[baseVertex + i2].normal, sheetFlag);
				outTriangles.push_back(tri);
			}
		}

		void traverseGltfNode(
			const json& root,
			int nodeIndex,
			const glm::mat4& parentMatrix,
			const std::vector<std::vector<uint8_t>>& buffers,
			std::vector<Vertex>& outVertices,
			std::vector<GLuint>& outIndices,
			std::vector<GpuTriangle>& outTriangles,
			glm::vec3& aabbMin,
			glm::vec3& aabbMax)
		{
			if (!root.contains("nodes") || nodeIndex < 0 || nodeIndex >= static_cast<int>(root["nodes"].size()))
			{
				return;
			}

			const json& node = root["nodes"][nodeIndex];
			const glm::mat4 worldMatrix = parentMatrix * computeNodeLocalMatrix(node);

			if (node.contains("mesh") && root.contains("meshes"))
			{
				int meshIndex = node["mesh"].get<int>();
				if (meshIndex >= 0 && meshIndex < static_cast<int>(root["meshes"].size()))
				{
					const json& mesh = root["meshes"][meshIndex];
					if (mesh.contains("primitives") && mesh["primitives"].is_array())
					{
						for (const auto& primitive : mesh["primitives"])
						{
							appendPrimitiveGeometry(
								root, primitive, buffers, worldMatrix,
								outVertices, outIndices, outTriangles, aabbMin, aabbMax);
						}
					}
				}
			}

			if (node.contains("children") && node["children"].is_array())
			{
				for (const auto& child : node["children"])
				{
					traverseGltfNode(
						root, child.get<int>(), worldMatrix, buffers,
						outVertices, outIndices, outTriangles, aabbMin, aabbMax);
				}
			}
		}

		void computeAngleWeightedNormals(std::vector<GpuTriangle>& triangles, const glm::vec3& aabbMin, const glm::vec3& aabbMax)
		{
			if (triangles.empty())
			{
				return;
			}

			float diag = glm::length(aabbMax - aabbMin);
			float weldCellSize = std::max(1e-6f, diag * 1e-5f);
			float invCellSize = 1.0f / weldCellSize;

			std::unordered_map<WeldKey, glm::vec3, WeldKeyHash> normalAccum;
			normalAccum.reserve(triangles.size());

			for (const auto& tri : triangles)
			{
				const glm::vec3 p0(tri.v0);
				const glm::vec3 p1(tri.v1);
				const glm::vec3 p2(tri.v2);
				const glm::vec3 faceNorm(tri.n0);

				float a0 = cornerAngle(p1 - p0, p2 - p0);
				float a1 = cornerAngle(p0 - p1, p2 - p1);
				float a2 = cornerAngle(p0 - p2, p1 - p2);

				normalAccum[makeWeldKey(p0, invCellSize)] += a0 * faceNorm;
				normalAccum[makeWeldKey(p1, invCellSize)] += a1 * faceNorm;
				normalAccum[makeWeldKey(p2, invCellSize)] += a2 * faceNorm;
			}

			auto resolveNormal = [&](const glm::vec3& p, const glm::vec3& fallback, float wFlag) -> glm::vec4 {
				auto it = normalAccum.find(makeWeldKey(p, invCellSize));
				if (it != normalAccum.end())
				{
					float len = glm::length(it->second);
					if (len > 1e-6f)
					{
						return glm::vec4(it->second / len, wFlag);
					}
				}
				return glm::vec4(fallback, wFlag);
			};

			for (auto& tri : triangles)
			{
				const glm::vec3 fallback(tri.n0);
				tri.n0 = resolveNormal(glm::vec3(tri.v0), fallback, tri.n0.w);
				tri.n1 = resolveNormal(glm::vec3(tri.v1), fallback, tri.n1.w);
				tri.n2 = resolveNormal(glm::vec3(tri.v2), fallback, tri.n2.w);
			}
		}
	} // namespace

	Model::Model(const std::string& gltfPath)
	{
		loadGLTF(gltfPath);
	}

	Model::~Model()
	{
		free();
	}

	Model::Model(Model&& other) noexcept
		: m_vao(other.m_vao)
		, m_vbo(other.m_vbo)
		, m_ebo(other.m_ebo)
		, m_indexCount(other.m_indexCount)
		, m_aabbMin(other.m_aabbMin)
		, m_aabbMax(other.m_aabbMax)
		, m_triangles(std::move(other.m_triangles))
		, m_textures(std::move(other.m_textures))
	{
		other.m_vao = 0;
		other.m_vbo = 0;
		other.m_ebo = 0;
		other.m_indexCount = 0;
	}

	Model& Model::operator=(Model&& other) noexcept
	{
		if (this != &other)
		{
			free();
			m_vao = other.m_vao;
			m_vbo = other.m_vbo;
			m_ebo = other.m_ebo;
			m_indexCount = other.m_indexCount;
			m_aabbMin = other.m_aabbMin;
			m_aabbMax = other.m_aabbMax;
			m_triangles = std::move(other.m_triangles);
			m_textures = std::move(other.m_textures);

			other.m_vao = 0;
			other.m_vbo = 0;
			other.m_ebo = 0;
			other.m_indexCount = 0;
		}
		return *this;
	}

	void Model::free()
	{
		if (m_vao != 0)
		{
			glDeleteVertexArrays(1, &m_vao);
			glDeleteBuffers(1, &m_vbo);
			glDeleteBuffers(1, &m_ebo);
			m_vao = 0;
			m_vbo = 0;
			m_ebo = 0;
			m_indexCount = 0;
		}
		for (auto& tex : m_textures)
		{
			tex.dispose();
		}
		m_textures.clear();
		m_triangles.clear();
		m_aabbMin = glm::vec3(0.0f);
		m_aabbMax = glm::vec3(0.0f);
	}

	bool Model::loadGLTF(const std::string& gltfPath)
	{
		free();

		std::ifstream gltfFile(gltfPath, std::ios::binary | std::ios::ate);
		if (!gltfFile.is_open())
		{
			std::cerr << "Failed to open glTF file: " << gltfPath << std::endl;
			return false;
		}

		const std::streamsize fileSize = gltfFile.tellg();
		if (fileSize <= 0)
		{
			std::cerr << "glTF file is empty: " << gltfPath << std::endl;
			return false;
		}
		gltfFile.seekg(0, std::ios::beg);
		std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
		if (!gltfFile.read(reinterpret_cast<char*>(fileBytes.data()), fileSize))
		{
			std::cerr << "Failed to read glTF file: " << gltfPath << std::endl;
			return false;
		}

		json gltfJson;
		std::vector<uint8_t> glbBinChunk;
		try
		{
			bool isGlb = false;
			if (fileBytes.size() >= 12)
			{
				uint32_t magic = 0;
				uint32_t version = 0;
				std::memcpy(&magic, fileBytes.data(), sizeof(uint32_t));
				std::memcpy(&version, fileBytes.data() + 4, sizeof(uint32_t));
				isGlb = (magic == 0x46546C67u && version == 2u);
			}

			if (isGlb)
			{
				uint32_t totalLength = 0;
				std::memcpy(&totalLength, fileBytes.data() + 8, sizeof(uint32_t));
				const size_t limit = std::min(fileBytes.size(), static_cast<size_t>(totalLength));
				size_t offset = 12;
				bool hasJsonChunk = false;

				while (offset + 8 <= limit)
				{
					uint32_t chunkLength = 0;
					uint32_t chunkType = 0;
					std::memcpy(&chunkLength, fileBytes.data() + offset, sizeof(uint32_t));
					std::memcpy(&chunkType, fileBytes.data() + offset + 4, sizeof(uint32_t));
					offset += 8;

					if (chunkLength > limit - offset)
					{
						std::cerr << "Invalid GLB chunk length in: " << gltfPath << std::endl;
						return false;
					}

					if (chunkType == 0x4E4F534Au && !hasJsonChunk)
					{
						const char* jsonPtr = reinterpret_cast<const char*>(fileBytes.data() + offset);
						gltfJson = json::parse(jsonPtr, jsonPtr + chunkLength);
						hasJsonChunk = true;
					}
					else if (chunkType == 0x004E4942u && glbBinChunk.empty())
					{
						glbBinChunk.assign(fileBytes.data() + offset, fileBytes.data() + offset + chunkLength);
					}

					offset += chunkLength;
				}

				if (!hasJsonChunk)
				{
					std::cerr << "GLB file has no JSON chunk: " << gltfPath << std::endl;
					return false;
				}
			}
			else
			{
				gltfJson = json::parse(fileBytes.begin(), fileBytes.end());
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "Failed to parse glTF JSON: " << e.what() << std::endl;
			return false;
		}

		std::filesystem::path baseDir = std::filesystem::path(gltfPath).parent_path();
		std::vector<std::vector<uint8_t>> buffers;
		if (!loadGltfBuffers(gltfJson, baseDir, glbBinChunk, buffers))
		{
			std::cerr << "Failed to load glTF buffers for: " << gltfPath << std::endl;
			return false;
		}

		std::vector<Vertex> vertices;
		std::vector<GLuint> indices;
		glm::vec3 aabbMin(std::numeric_limits<float>::max());
		glm::vec3 aabbMax(std::numeric_limits<float>::lowest());

		if (gltfJson.contains("scenes") && gltfJson["scenes"].is_array() && !gltfJson["scenes"].empty())
		{
			int sceneIdx = gltfJson.value("scene", 0);
			sceneIdx = std::clamp(sceneIdx, 0, static_cast<int>(gltfJson["scenes"].size()) - 1);
			const json& sceneObj = gltfJson["scenes"][sceneIdx];
			if (sceneObj.contains("nodes") && sceneObj["nodes"].is_array())
			{
				for (const auto& nodeId : sceneObj["nodes"])
				{
					traverseGltfNode(
						gltfJson, nodeId.get<int>(), glm::mat4(1.0f), buffers,
						vertices, indices, m_triangles, aabbMin, aabbMax);
				}
			}
		}
		else if (gltfJson.contains("nodes") && gltfJson["nodes"].is_array())
		{
			std::vector<bool> isChild(gltfJson["nodes"].size(), false);
			for (const auto& node : gltfJson["nodes"])
			{
				if (node.contains("children") && node["children"].is_array())
				{
					for (const auto& c : node["children"])
					{
						int cid = c.get<int>();
						if (cid >= 0 && cid < static_cast<int>(isChild.size()))
						{
							isChild[cid] = true;
						}
					}
				}
			}
			for (size_t i = 0; i < isChild.size(); ++i)
			{
				if (!isChild[i])
				{
					traverseGltfNode(
						gltfJson, static_cast<int>(i), glm::mat4(1.0f), buffers,
						vertices, indices, m_triangles, aabbMin, aabbMax);
				}
			}
		}
		else if (gltfJson.contains("meshes") && gltfJson["meshes"].is_array())
		{
			for (const auto& mesh : gltfJson["meshes"])
			{
				if (mesh.contains("primitives") && mesh["primitives"].is_array())
				{
					for (const auto& primitive : mesh["primitives"])
					{
						appendPrimitiveGeometry(
							gltfJson, primitive, buffers, glm::mat4(1.0f),
							vertices, indices, m_triangles, aabbMin, aabbMax);
					}
				}
			}
		}

		if (vertices.empty() || indices.empty() || m_triangles.empty())
		{
			std::cerr << "glTF model contains no valid triangle geometry: " << gltfPath << std::endl;
			free();
			return false;
		}

		m_aabbMin = aabbMin;
		m_aabbMax = aabbMax;
		if (m_triangles.size() <= 50000)
		{
			computeAngleWeightedNormals(m_triangles, m_aabbMin, m_aabbMax);
		}

		m_indexCount = static_cast<GLsizei>(indices.size());

		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ebo);

		glBindVertexArray(m_vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
		glEnableVertexAttribArray(1);

		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
		glEnableVertexAttribArray(2);

		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texUV)));
		glEnableVertexAttribArray(3);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		return true;
	}

	glm::mat4 Model::computeFitTransform(float worldSize, const glm::vec3& worldOffset, float paddingFraction) const
	{
		if (m_triangles.empty())
		{
			return glm::mat4(1.0f);
		}

		glm::vec3 center = 0.5f * (m_aabbMin + m_aabbMax);
		glm::vec3 extent = m_aabbMax - m_aabbMin;
		float maxExtent = std::max({extent.x, extent.y, extent.z, 1e-5f});
		float usableFraction = std::clamp(1.0f - paddingFraction, 0.1f, 0.99f);
		float scale = (worldSize * usableFraction) / maxExtent;

		glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldOffset);
		mat = glm::scale(mat, glm::vec3(scale));
		mat = glm::translate(mat, -center);
		return mat;
	}

	std::vector<GpuTriangle> Model::buildGpuTriangles(const glm::mat4& transform) const
	{
		std::vector<GpuTriangle> result;
		result.reserve(m_triangles.size());

		const glm::mat3 linearMat(transform);
		const glm::mat3 normalMat = glm::transpose(glm::inverse(linearMat));
		const bool flipWinding = glm::determinant(linearMat) < 0.0f;

		for (const auto& src : m_triangles)
		{
			GpuTriangle dst{};
			dst.v0 = glm::vec4(glm::vec3(transform * glm::vec4(glm::vec3(src.v0), 1.0f)), 0.0f);
			glm::vec3 p1 = glm::vec3(transform * glm::vec4(glm::vec3(src.v1), 1.0f));
			glm::vec3 p2 = glm::vec3(transform * glm::vec4(glm::vec3(src.v2), 1.0f));

			glm::vec3 n0 = glm::normalize(normalMat * glm::vec3(src.n0));
			glm::vec3 n1 = glm::normalize(normalMat * glm::vec3(src.n1));
			glm::vec3 n2 = glm::normalize(normalMat * glm::vec3(src.n2));

			if (flipWinding)
			{
				dst.v1 = glm::vec4(p2, 0.0f);
				dst.v2 = glm::vec4(p1, 0.0f);
				dst.n0 = glm::vec4(-n0, src.n0.w);
				dst.n1 = glm::vec4(-n2, src.n2.w);
				dst.n2 = glm::vec4(-n1, src.n1.w);
			}
			else
			{
				dst.v1 = glm::vec4(p1, 0.0f);
				dst.v2 = glm::vec4(p2, 0.0f);
				dst.n0 = glm::vec4(n0, src.n0.w);
				dst.n1 = glm::vec4(n1, src.n1.w);
				dst.n2 = glm::vec4(n2, src.n2.w);
			}

			result.push_back(dst);
		}

		return result;
	}

	void Model::draw(const Shader& shader, const glm::mat4& modelMatrix, int materialIndex) const
	{
		if (m_vao == 0 || m_indexCount == 0)
			return;

		shader.setUniform("u_model", modelMatrix);
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
		shader.setUniform("u_normalMatrix", normalMatrix);
		shader.setUniform("u_materialIndex", materialIndex);
		shader.setUniform("u_hasDiffuse", m_textures.empty() ? 0 : 1);

		if (!m_textures.empty())
		{
			m_textures[0].bind(0);
		}

		glBindVertexArray(m_vao);
		glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
		glBindVertexArray(0);
	}
} // namespace renderer
