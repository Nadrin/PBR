/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#include <cstdio>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>

#include "mesh.hpp"

namespace {
	const unsigned int ImportFlags = 
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_SortByPType |
		aiProcess_PreTransformVertices |
		aiProcess_GenNormals |
		aiProcess_GenUVCoords |
		aiProcess_OptimizeMeshes |
		aiProcess_Debone |
		aiProcess_ValidateDataStructure;
}

struct LogStream : public Assimp::LogStream
{
	static void initialize()
	{
		if(Assimp::DefaultLogger::isNullLogger()) {
			Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
			Assimp::DefaultLogger::get()->attachStream(new LogStream, Assimp::Logger::Err | Assimp::Logger::Warn);
		}
	}
	
	void write(const char* message) override
	{
		std::fprintf(stderr, "Assimp: %s", message);
	}
};

Mesh::Mesh(const aiMesh* mesh)
{
	assert(mesh->HasPositions());
	assert(mesh->HasNormals());

	m_vertices.reserve(mesh->mNumVertices);
	for(size_t i=0; i<m_vertices.capacity(); ++i) {
		Vertex vertex;
		vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
		vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
		if(mesh->HasTangentsAndBitangents()) {
			vertex.tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
			vertex.bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};
		}
		if(mesh->HasTextureCoords(0)) {
			vertex.texcoord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
		}
		m_vertices.push_back(vertex);
	}
	
	m_faces.reserve(mesh->mNumFaces);
	for(size_t i=0; i<m_faces.capacity(); ++i) {
		assert(mesh->mFaces[i].mNumIndices == 3);
		m_faces.push_back({mesh->mFaces[i].mIndices[0], mesh->mFaces[i].mIndices[1], mesh->mFaces[i].mIndices[2]});
	}
}

std::shared_ptr<Mesh> Mesh::fromFile(const std::string& filename)
{
	LogStream::initialize();

	std::printf("Loading mesh: %s\n", filename.c_str());
	
	std::shared_ptr<Mesh> mesh;
	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFile(filename, ImportFlags);
	if(scene && scene->HasMeshes()) {
		mesh = std::shared_ptr<Mesh>(new Mesh{scene->mMeshes[0]});
	}
	else {
		throw std::runtime_error("Failed to load mesh file: " + filename);
	}
	return mesh;
}

std::shared_ptr<Mesh> Mesh::fromString(const std::string& data)
{
	LogStream::initialize();

	std::shared_ptr<Mesh> mesh;
	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFileFromMemory(data.c_str(), data.length(), ImportFlags, "nff");
	if(scene && scene->HasMeshes()) {
		mesh = std::shared_ptr<Mesh>(new Mesh{scene->mMeshes[0]});
	}
	else {
		throw std::runtime_error("Failed to create mesh from string: " + data);
	}
	return mesh;
}
