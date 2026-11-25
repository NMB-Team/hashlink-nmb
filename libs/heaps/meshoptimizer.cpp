#define HL_NAME(n) heaps_##n
#include <meshoptimizer.h>
#include <hl.h>
#include <memory>

HL_PRIM int HL_NAME(optimize_mesh)(float* vertices, int vertexCount, int vertexSize, unsigned int* indices, int* indexCount, int targetIndexCount, float targetError) {
	if (*indexCount == 0)
		return 0;

	std::unique_ptr<unsigned char[]> vertexScratch(new unsigned char[size_t(vertexCount) * vertexSize]);
	std::unique_ptr<unsigned int[]> indexScratch(new unsigned int[*indexCount]);

	{
		std::unique_ptr<unsigned int[]> remap(new unsigned int[vertexCount]);
		int uniqueVertexCount = (int)meshopt_generateVertexRemap(remap.get(), indices, *indexCount, vertices, vertexCount, vertexSize);
		meshopt_remapVertexBuffer(vertexScratch.get(), vertices, vertexCount, vertexSize, remap.get());
		meshopt_remapIndexBuffer(indices, indices, *indexCount, remap.get());
		vertexCount = uniqueVertexCount;
	}

	const float* positions = reinterpret_cast<const float*>(vertexScratch.get());
	if (targetIndexCount >= 0) {
		if (targetIndexCount > *indexCount)
			targetIndexCount = *indexCount;

		unsigned int options = meshopt_SimplifyLockBorder | meshopt_SimplifyPrune;
		*indexCount = (int)meshopt_simplify(indices, indices, *indexCount, positions, vertexCount, vertexSize, targetIndexCount, targetError, options, nullptr);
	}

	meshopt_optimizeVertexCache(indexScratch.get(), indices, *indexCount, vertexCount);
	meshopt_optimizeOverdraw(indices, indexScratch.get(), *indexCount, positions, vertexCount, vertexSize, 1.05f);

	return (int)meshopt_optimizeVertexFetch(vertices, indices, *indexCount, positions, vertexCount, vertexSize);
}

HL_PRIM int HL_NAME(generate_vertex_remap)(unsigned int* pRemapOut, unsigned int* pIndices, int indexCount, float* pVertices, int vertexCount, int vertexSize) {
	return (int)meshopt_generateVertexRemap(pRemapOut, pIndices, indexCount, pVertices, vertexCount, vertexSize);
}

HL_PRIM void HL_NAME(remap_index_buffer)(unsigned int* pIndicesOut, unsigned int* pIndicesIn, int indexCount, unsigned int* pRemap) {
	meshopt_remapIndexBuffer(pIndicesOut, pIndicesIn, indexCount, pRemap);
}

HL_PRIM void HL_NAME(remap_vertex_buffer)(void* pVerticesOut, void* pVerticexIn, int vertexCount, int vertexSize, unsigned int* pRemap) {
	meshopt_remapVertexBuffer(pVerticesOut, pVerticexIn, vertexCount, vertexSize, pRemap);
}

HL_PRIM int HL_NAME(simplify)(unsigned int* pIndicesOut, unsigned int* pIndicesIn, int indexCount, float* pVertices, int vertexCount, int vertexSize, int targetIndexCount, float targetError, int options, float* resultErrorOut) {
	return (int)meshopt_simplify(pIndicesOut, pIndicesIn, indexCount, pVertices, vertexCount, vertexSize, targetIndexCount, (float)targetError, options, resultErrorOut);
}

HL_PRIM int HL_NAME(simplifyWithUpdate)(unsigned int* pIndicesOut, int indexCount, float* pVertexPos, int vertexCount, int vertexStride,
	float* pVertexAttributes, unsigned int attributeStride, const float* pAttributeWeights, unsigned int attributeCount,
	const unsigned char* pVertexLock, int targetIndexCount, float targetError, unsigned int options, float* resultErrorOut) {
	return (int)meshopt_simplifyWithUpdate(pIndicesOut, indexCount, pVertexPos, vertexCount, vertexStride, pVertexAttributes, attributeStride, pAttributeWeights, attributeCount, pVertexLock, targetIndexCount, targetError, options, resultErrorOut);
}

HL_PRIM void HL_NAME(optimize_vertex_cache)(unsigned int* pIndicesOut, unsigned int* pIndicesIn, int indexCount, int vertexCount) {
	meshopt_optimizeVertexCache(pIndicesOut, pIndicesIn, indexCount, vertexCount);
}

HL_PRIM void HL_NAME(optimize_overdraw)(unsigned int* pIndicesOut, unsigned int* pIndicesIn, int indexCount, float* pVertices, int vertexCount, int vertexSize, float threshold) {
	meshopt_optimizeOverdraw(pIndicesOut, pIndicesIn, indexCount, pVertices, vertexCount, vertexSize, threshold);
}

HL_PRIM int HL_NAME(optimize_vertex_fetch)(float* pVerticesOut, unsigned int* pIndices, int indexCount, void* pVerticesIn, int vertexCount, int vertexSize) {
	return (int)meshopt_optimizeVertexFetch(pVerticesOut, pIndices, indexCount, pVerticesIn, vertexCount, vertexSize);
}

HL_PRIM void HL_NAME(generate_tangents)(float* pTangentsOut, unsigned int* pIndices, int indexCount, float* pVertexPos, int vertexCount, int posStride, float* pVertexNormals, int normalStride, float* pVertexUVs, int uvStride, unsigned int options) {
	meshopt_generateTangents(pTangentsOut, pIndices, indexCount, pVertexPos, vertexCount, posStride, pVertexNormals, normalStride, pVertexUVs, uvStride, options);
}

DEFINE_PRIM(_I32, optimize_mesh, _BYTES _I32 _I32 _BYTES _REF(_I32) _I32 _F32);
DEFINE_PRIM(_I32, generate_vertex_remap, _BYTES _BYTES _I32 _BYTES _I32 _I32);
DEFINE_PRIM(_VOID, remap_index_buffer, _BYTES _BYTES _I32 _BYTES);
DEFINE_PRIM(_VOID, remap_vertex_buffer, _BYTES _BYTES _I32 _I32 _BYTES);
DEFINE_PRIM(_I32, simplify, _BYTES _BYTES _I32 _BYTES _I32 _I32 _I32 _F32 _I32 _BYTES);
DEFINE_PRIM(_I32, simplifyWithUpdate, _BYTES _I32 _BYTES _I32 _I32 _BYTES _I32 _BYTES _I32 _BYTES _I32 _F32 _I32 _BYTES);
DEFINE_PRIM(_VOID, optimize_vertex_cache, _BYTES _BYTES _I32 _I32);
DEFINE_PRIM(_VOID, optimize_overdraw, _BYTES _BYTES _I32 _BYTES _I32 _I32 _F32);
DEFINE_PRIM(_I32, optimize_vertex_fetch, _BYTES _BYTES _I32 _BYTES _I32 _I32);
DEFINE_PRIM(_VOID, generate_tangents, _BYTES _BYTES _I32 _BYTES _I32 _I32 _BYTES _I32 _BYTES _I32 _I32);
