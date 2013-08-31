/**
Author: Jialu Hu
Date: Jun. 11, 2013
File name: algorithm/search.h
Description: Searching high-scoring subnetworks.
**/
#pragma once
#ifndef SEARCH_H_
#define SEARCH_H_

#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <lemon/list_graph.h>
#include <lemon/smart_graph.h>
#include <lemon/connectivity.h>
#include "verbose.h"
#include "input/tree.h"
#include "algorithm/phylogeny.h"
#include "algorithm/simulatedannealing.h"
#include "function.h"

template<typename NP, typename SN, typename LG, typename OP>
class Search
{
private:
	typedef NP NetworkPool;
	typedef SN SubNet;
	typedef LG LayerGraph;
	typedef OP Option;
	typedef typename LayerGraph::Graph Graph;
	typedef typename SubNet::K_Spine K_Spine;
	typedef typename SubNet::GraphData GraphData;
	typedef Tree<Graph, Option> MyTree;
	typedef Phylogeny<SubNet,MyTree> MyPhylogeny;
	typedef SimulatedAnnealing<MyPhylogeny,Option> MySimulatedAnnealing;
public:
  TEMPLATE_GRAPH_TYPEDEFS(Graph);
  /// Labels of the nodes.
  typedef typename Graph::template NodeMap<std::string> OrigLabelNodeMap;
  /// Mapping from labels to original nodes.
  typedef std::unordered_map<std::string, typename Graph::Node> InvOrigLabelNodeMap;

    unsigned _numSpecies;
    int _seedSize;
    int _seedTries;
    int _numSamples;
	int _minExt;
	int _maxExt;
    int _numExtension;
    int _numConnected;
    int _numthreads;
	std::string _resultfolder;
	std::string _treefile;
	std::vector<std::string> _speciesfiles;
    
    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution;
    
    typename std::vector<SubNet*> refinedSeeds;

    typedef struct _PrivateVariable
	{
		MyPhylogeny phylogeny;
		SubNet subnet;
		int k;
		int j;
		unsigned ei;
		unsigned ej;
		int num;
		int numExtension;
		int numConnected;
		int nodeid;
		std::vector<Node> candidates;
		std::unordered_map<int,bool> usedValidNodes;
		Node node;
		typename std::vector<Node>::iterator it;
		_PrivateVariable()
		:phylogeny(),subnet(3,5),k(0),j(0),ei(0),ej(0),num(0),numExtension(0),numConnected(0),candidates(),usedValidNodes()
		{
		}
	} PrivateVariable;
	Search(Option&);
	~Search(){};
	void run(LayerGraph&,NetworkPool&);
	void setExtension(PrivateVariable&);
	void searchSeeds(LayerGraph&,NetworkPool&);
	void searchCandidates(std::unordered_map<int,bool>&,std::vector<Node>&,K_Spine&,LayerGraph&,NetworkPool&);
	bool sampleSeed(SubNet&,LayerGraph&,NetworkPool&,std::uniform_int_distribution<int>&);
	int sampleStringElement(int);
	bool sampleKSpine(Node&,K_Spine&,LayerGraph&,NetworkPool&);
	bool expandspine(K_Spine,K_Spine&,std::vector<Node>,Node,LayerGraph&,NetworkPool&,unsigned);
	void verifyspine(LayerGraph&,NetworkPool&);
	bool checkConnection(PrivateVariable&,LayerGraph&,NetworkPool&);
	void expandRefinedSeeds(PrivateVariable&,LayerGraph&,NetworkPool&);
	bool heuristicSearch(SubNet&,std::unordered_map<int,bool>&,std::vector<Node>&,LayerGraph&,NetworkPool&);
	
};

template<typename NP, typename SN, typename LG, typename OP>
Search<NP,SN,LG,OP>::Search(Option& myoption)
:generator(std::chrono::system_clock::now().time_since_epoch().count())
,distribution(0,10000)
,refinedSeeds()
{
	_numSpecies=myoption.numspecies;
	_seedSize=myoption.seedsize;
	_seedTries=myoption.seedtries;
	_numSamples=myoption.numsamples;
	_numConnected=myoption.numconnected;
	_numExtension=myoption.minext;
	_minExt=myoption.minext;
	_maxExt=myoption.maxext;
	_resultfolder=myoption.resultfolder;
	_numthreads=myoption.numthreads;
	_treefile=myoption.treefile;
	_speciesfiles=myoption.speciesfiles;
}

template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::run(LayerGraph& layergraph,NetworkPool& networks)
{
	searchSeeds(layergraph,networks);
	MySimulatedAnnealing simulatedannealing;
	int numAll=0;
	unsigned csize=refinedSeeds.size();
	PrivateVariable myPrivateVariable;
	#pragma omp parallel for num_threads(_numthreads) schedule(dynamic,1) shared(layergraph,networks,simulatedannealing,csize) private(myPrivateVariable) reduction(+ : numAll)
	for(unsigned i=0;i<csize;i++)
	{
		for(myPrivateVariable.k=_minExt;myPrivateVariable.k<=_maxExt;myPrivateVariable.k++)
		{
			setExtension(myPrivateVariable);
			myPrivateVariable.phylogeny.setDsize(_seedSize+myPrivateVariable.k);
			for(myPrivateVariable.j=0; myPrivateVariable.j<_seedTries;myPrivateVariable.j++)//_seedTries
			{
				myPrivateVariable.subnet=*refinedSeeds[i];
				expandRefinedSeeds(myPrivateVariable,layergraph,networks);
				myPrivateVariable.subnet.induceSubgraphs(networks,layergraph);
				if(checkConnection(myPrivateVariable,layergraph,networks))
				{
					//std::string filename;
					myPrivateVariable.phylogeny.initial(_treefile,_speciesfiles,myPrivateVariable.subnet,layergraph);
					simulatedannealing.run(myPrivateVariable.phylogeny);
					//numQualified++;
					numAll++;
					myPrivateVariable.subnet.outputAlignment(myPrivateVariable.phylogeny._tree.overallScore,
															layergraph,
															_resultfolder,
															i,
															myPrivateVariable.k,
															myPrivateVariable.j);
					//if(g_verbosity>=VERBOSE_NON_ESSENTIAL)
					//{
						//_phylogeny.outputInternalGraphs();
						//std::cout << "--------------------------------------" << std::endl;
					//}
				}
			}
		}
	}
	std::cout <<numAll<<" subnets have been found for all seeds."<<std::endl;
}

template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::setExtension(PrivateVariable& myPrivateVariable)
{
	myPrivateVariable.numExtension=myPrivateVariable.k;
	myPrivateVariable.phylogeny.setDsize(_seedSize+myPrivateVariable.k);
}

template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::searchCandidates(std::unordered_map<int,bool>& usedValidNodes,
																			std::vector<Node>& candidates,
																			K_Spine& pspine,
																			LayerGraph& layergraph,
																			NetworkPool& networks)
{
	std::vector<std::string> innerproteins;
	std::vector<std::string> neighborproteins;
	for(unsigned j=0;j<_numSpecies;j++)
	{
		innerproteins.push_back(layergraph.node2label[pspine.data[j]]);
	}
	networks.getNeighbors(innerproteins,neighborproteins);
	for(unsigned j=0;j<neighborproteins.size();++j)
	{
		std::string protein=neighborproteins[j];
		Node neighbor=layergraph.label2node[protein];
		int neighborid=layergraph.graph.id(neighbor);
		if(layergraph.validnodemap.find(neighborid)==layergraph.validnodemap.end())continue;
		if(usedValidNodes.find(neighborid)!=usedValidNodes.end())continue;
		if(find(candidates.begin(),candidates.end(),neighbor)!=candidates.end())continue;
		candidates.push_back(neighbor);
	}
}

template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::expandRefinedSeeds(PrivateVariable& myprivateVariable,
										LayerGraph& layergraph,
										NetworkPool& networks)
{
	
	//// searching neighbors of subnet.
	for(myprivateVariable.ei=0;myprivateVariable.ei<myprivateVariable.subnet.net_spines.size();myprivateVariable.ei++)
	{
		for(myprivateVariable.ej=0;myprivateVariable.ej<_numSpecies;myprivateVariable.ej++)
		{
			myprivateVariable.node=myprivateVariable.subnet.net_spines[myprivateVariable.ei].data[myprivateVariable.ej];
			myprivateVariable.nodeid=layergraph.graph.id(myprivateVariable.node);
			if(layergraph.validnodemap.find(myprivateVariable.nodeid)==layergraph.validnodemap.end())continue;
			myprivateVariable.usedValidNodes[layergraph.graph.id(myprivateVariable.node)]=true;
		}
		searchCandidates(myprivateVariable.usedValidNodes,
						myprivateVariable.candidates, 
						myprivateVariable.subnet.net_spines[myprivateVariable.ei], 
						layergraph, 
						networks);
	}
	myprivateVariable.it=myprivateVariable.candidates.begin();
	while(myprivateVariable.it!=myprivateVariable.candidates.end())
	{
		if(myprivateVariable.usedValidNodes.find(layergraph.graph.id(*myprivateVariable.it))
		  !=myprivateVariable.usedValidNodes.end())
			myprivateVariable.it=myprivateVariable.candidates.erase(myprivateVariable.it);
		else
			++myprivateVariable.it;
	}
	while(myprivateVariable.num++<_numExtension)
	{
		heuristicSearch(myprivateVariable.subnet, 
						myprivateVariable.usedValidNodes, 
						myprivateVariable.candidates, 
						layergraph,
						networks);
	}	
}

template<typename NP, typename SN, typename LG, typename OP>
bool
	Search<NP,SN,LG,OP>::heuristicSearch(SubNet& subnet, std::unordered_map<int,bool>& usedValidNodes, std::vector<Node>& candidates,LayerGraph& layergraph,NetworkPool& networks)
{
	//std::vector<Node> optimalcandidates;
	//std::unordered_map<int,int> candidatesmap;
	//int maxnum=1;
	//for(unsigned i=0;i<candidates.size();i++)
	//{
	//	int nodeid=layergraph.graph.id(candidates[i]);
	//	if(candidatesmap.find(nodeid)!=candidatesmap.end())
	//	{
	//		candidatesmap[nodeid]++;
	//		if(maxnum<candidatesmap[nodeid])
	//			maxnum=candidatesmap[nodeid];
	//	}
	//	else
	//		candidatesmap[nodeid]=1;
	//}
	//for(std::unordered_map<int,int>::iterator it=candidatesmap.begin();it!=candidatesmap.end();it++)
	//{
	//	if(it->second==maxnum)
	//		optimalcandidates.push_back(layergraph.graph.nodeFromId(it->first));
	//}
	///
	/*int dice_roll = distribution(generator)%optimalcandidates.size();
	Node node=optimalcandidates[dice_roll];*/
	unsigned csize=candidates.size();
	std::uniform_int_distribution<int> discrete(0,layergraph.validnodes.size()-1);
	Node node;
	int dice_roll;
	if(csize == 0)
	{
		dice_roll = discrete(generator);
		node=layergraph.validnodes[dice_roll];
		while(usedValidNodes.find(layergraph.graph.id(node))!=usedValidNodes.end())
		{
			dice_roll = discrete(generator);
			node=layergraph.validnodes[dice_roll];
		}
	}else
	{
		dice_roll = distribution(generator)%csize;
		node=candidates[dice_roll];
		candidates.erase(candidates.begin()+dice_roll);
	}
	
	K_Spine pspine;
	usedValidNodes[layergraph.graph.id(node)]=true;
	if(!sampleKSpine(node,pspine,layergraph,networks))
	{
		std::cerr <<"Invalid sample node!"<<std::endl;
		return false;
	}
	subnet.net_spines.push_back(pspine);
	searchCandidates(usedValidNodes, candidates,pspine,layergraph,networks);
	
	return true;
}

template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::searchSeeds(LayerGraph& layergraph,NetworkPool& networks)
{
	verifyspine(layergraph,networks);

	std::uniform_int_distribution<int> discrete(0,layergraph.validnodes.size()-1);
	int num=0;
	while(num++<_numSamples)
	{
		SubNet* mysubnet= new SubNet(_numSpecies,_seedSize);
		if(!sampleSeed(*mysubnet,layergraph,networks,discrete))
		{
			delete mysubnet;
			if(g_verbosity>=VERBOSE_NON_ESSENTIAL)
			std::cerr << "Failed to sample a refined seed!" << std::endl;
		}else
		{
			refinedSeeds.push_back(mysubnet);
		}
	}
	std::cerr << "There are a total of "<< refinedSeeds.size() <<" refined seeds."<< std::endl;
}

/// Randomly sample a d subnet from G_h
template<typename NP, typename SN, typename LG, typename OP>
bool
Search<NP,SN,LG,OP>::sampleSeed(SubNet& subnet, LayerGraph& layergraph, NetworkPool& networks,std::uniform_int_distribution<int>& discrete)
{
	/// Randomly sample a k-spine containing node.
	int dice_roll = discrete(generator);
	Node firstnode = layergraph.validnodes[dice_roll];
	std::vector<Node> candidates;
	std::unordered_map<int,bool> usedValidNodes;
	for(int i=0;i<_seedSize;++i)
	{
		Node node;
		if(0==i) node = firstnode;
		else if(candidates.size()==0)
		{
			dice_roll = discrete(generator);
			node=layergraph.validnodes[dice_roll];
		}
		else
		{
			/// choose the optimal neighbor
			//std::vector<Node> optimalcandidates;
			//std::unordered_map<int,int> candidatesmap;
			//int maxnum=1;
			//for(unsigned i=0;i<candidates.size();i++)
			//{
				//int nodeid=layergraph.graph.id(candidates[i]);
				//if(candidatesmap.find(nodeid)!=candidatesmap.end())
				//{
					//candidatesmap[nodeid]++;
					//if(maxnum<candidatesmap[nodeid])
						//maxnum=candidatesmap[nodeid];
				//}
				//else
					//candidatesmap[nodeid]=1;
			//}
			//for(std::unordered_map<int,int>::iterator it=candidatesmap.begin();it!=candidatesmap.end();it++)
			//{
				//if(it->second==maxnum)
					//optimalcandidates.push_back(layergraph.graph.nodeFromId(it->first));
			//}
			/// randomly sample a neighbor in candidates.
			dice_roll = distribution(generator)%candidates.size();
			node=candidates[dice_roll];
			candidates.erase(candidates.begin()+dice_roll);
		}
		K_Spine pspine;
		usedValidNodes[layergraph.graph.id(node)]=true;
		if(!sampleKSpine(node,pspine,layergraph,networks))
		{
			std::cerr <<"Invalid sample node!"<<std::endl;
			return false;
		}

		/// Output this sample.
		if(g_verbosity >= VERBOSE_NON_ESSENTIAL)
		{
			for(unsigned j=0;j<_numSpecies;++j)
				std::cout << layergraph.node2label[pspine.data[j]]<<" ";
			std::cout << std::endl;
		}
		searchCandidates(usedValidNodes,candidates, pspine, layergraph, networks);
		subnet.net_spines.push_back(pspine);
	}
	subnet.induceSubgraphs(networks,layergraph);
	PrivateVariable myPrivateVariable;
	myPrivateVariable.subnet=subnet;
	return checkConnection(myPrivateVariable,layergraph,networks);
}

/// Collect these nodes which can conduct a successful sample of k-spine.
template<typename NP, typename SN, typename LG, typename OP>
void
Search<NP,SN,LG,OP>::verifyspine(LayerGraph& layergraph,NetworkPool& networks)
{
	for(NodeIt node(layergraph.graph); node!=lemon::INVALID; ++node)
	{
		K_Spine pspine;
		if(sampleKSpine(node,pspine,layergraph,networks))
		{
			layergraph.setConfiguration(node);
		}
	}
}

template<typename NP, typename SN, typename LG, typename OP>
bool
Search<NP,SN,LG,OP>::checkConnection(PrivateVariable& myprivateVariable,LayerGraph& layergraph,NetworkPool& networks)
{
	myprivateVariable.numConnected=0;
	for(unsigned i=0;i<_numSpecies;++i)
	{
		if(myprivateVariable.subnet.subgraphs[i]->edgeNum< (myprivateVariable.subnet.subgraphs[i]->nodeNum-2) )continue;/// To fulfills the minimal number of edges. 
		myprivateVariable.numConnected++;
	}
	if(g_verbosity>=VERBOSE_NON_ESSENTIAL)
	std::cout << "There are "<<myprivateVariable.numConnected << " out of " <<_numSpecies <<" subnetwork(s) are connected!" << std::endl;
	return myprivateVariable.numConnected >= _numConnected;
}

template<typename NP, typename SN, typename LG, typename OP>
bool
Search<NP,SN,LG,OP>::sampleKSpine(Node& node,K_Spine& pspine,LayerGraph& layergraph,NetworkPool& networks)
{
	std::vector<Node> candidates;//
	K_Spine spine;
	unsigned k=_numSpecies;
	unsigned host=networks.getHost(layergraph.node2label[node]);
	spine.data[host]=node;
	spine.states[host]=true;
	if(g_verbosity==VERBOSE_DEBUG)
	std::cerr << layergraph.node2label[node] << std::endl;
	
	for(IncEdgeIt it(layergraph.graph,node);it!=lemon::INVALID;++it)
	{
		Node rnode=layergraph.graph.runningNode(it);
		host=networks.getHost(layergraph.node2label[rnode]);
		if(g_verbosity==VERBOSE_DEBUG)
		std::cerr << layergraph.node2label[rnode] << std::endl;
		if(spine.states[host])continue;
		if(node < rnode) candidates.push_back(rnode);
	}
	
	return expandspine(spine,pspine,candidates, node,layergraph,networks,k);
}

template<typename NP, typename SN, typename LG, typename OP>
int
Search<NP,SN,LG,OP>::sampleStringElement(int up)
{
    int dice_roll = distribution(generator)%(up);
    return dice_roll;
}
template<typename NP, typename SN, typename LG, typename OP>
bool
Search<NP,SN,LG,OP>::expandspine(	K_Spine spine
								, K_Spine& pspine
								, std::vector<Node> candidates
								, Node node					
								, LayerGraph& layergraph
								, NetworkPool& networks
								, unsigned k)
{

	if(0==--k)
	{
		pspine=spine;
		return true;
	}

	while(!candidates.empty())
	{
		unsigned host;
		int size_Candidates=candidates.size();
		typename std::vector<Node>::iterator in=candidates.begin()+sampleStringElement(size_Candidates);
		Node w = *in;
		candidates.erase(in);
		std::vector<Node> secondCandidates;//V'extention
		std::vector<Node> exclCandidates;//ExclCandidates  V'UN(V')
		/// Assign V'UN(V') to exclCandidates.
		for(unsigned i=0;i<_numSpecies;i++)
		{
			if(!spine.states[i])continue;
			Node spinenode=spine.data[i];
			exclCandidates.push_back(spinenode);
	
			for(IncEdgeIt it(layergraph.graph,spinenode);it!=lemon::INVALID;++it)
			{
				Node rnode=layergraph.graph.runningNode(it);
				host=networks.getHost(layergraph.node2label[rnode]);
				if(spine.states[host])continue;/// If yes, it's not a neighbor.			
				if( std::find(exclCandidates.begin(), exclCandidates.end(), rnode)
				    != exclCandidates.end())continue;
				exclCandidates.push_back(rnode);			
			}
		}
		/// Assign new value to secondCandidates.
		for(IncEdgeIt it(layergraph.graph,w);it!=lemon::INVALID;++it)
		{
			Node rnode=layergraph.graph.runningNode(it);
			host=networks.getHost(layergraph.node2label[rnode]);
			if(spine.states[host])continue;
			if( std::find(exclCandidates.begin(), exclCandidates.end(), rnode)
				    != exclCandidates.end())continue;
			if (node < rnode) secondCandidates.push_back(rnode);				
		}
		K_Spine newspine(spine);
		host=networks.getHost(layergraph.node2label[w]);
		if(g_verbosity==VERBOSE_DEBUG)
		std::cerr << layergraph.node2label[w] << std::endl;
		newspine.data[host]=w;
		newspine.states[host]=true;
		for(unsigned i=0;i<candidates.size();++i)
		{
			Node rnode=candidates[i];
			host=networks.getHost(layergraph.node2label[rnode]);
			if(newspine.states[host])continue;
			secondCandidates.push_back(rnode);
		}
		if(expandspine(newspine, pspine, secondCandidates, node, layergraph, networks, k))return true;
	}
	return false;
}
#endif
