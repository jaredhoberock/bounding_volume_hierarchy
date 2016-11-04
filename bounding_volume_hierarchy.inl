#include "bounding_volume_hierarchy.hpp"
#include <iostream>
#include <limits>
#include <algorithm>
#include <cassert>

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  const typename bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::NodeIndex bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::NULL_NODE = std::numeric_limits<NodeIndex>::max();

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  const float bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::EPS = 0.00005f;

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  typename bounding_volume_hierarchy<PrimitiveType, PointType, RealType>::NodeIndex
    bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::addNode(const NodeIndex parent)
{
  NodeIndex result = nodes_.size();

  nodes_.emplace_back(parent);

  return result;
} // end bounding_volume_hierarchy::addNode()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  bool bounding_volume_hierarchy<PrimitiveType,PointType,RealType>
    ::intersectBox(const Point &o, const Point &invDir,
                   const Point &minBounds, const Point &maxBounds,
                   const Real &tMin, const Real &tMax)
{
  Point tMin3, tMax3;
  for(int i = 0; i < 3; ++i)
  {
    tMin3[i] = (minBounds[i] - o[i]) * invDir[i];
    tMax3[i] = (maxBounds[i] - o[i]) * invDir[i];
  }

  Point tNear3(std::min(tMin3[0], tMax3[0]),
               std::min(tMin3[1], tMax3[1]),
               std::min(tMin3[2], tMax3[2]));
  Point  tFar3(std::max(tMin3[0], tMax3[0]),
               std::max(tMin3[1], tMax3[1]),
               std::max(tMin3[2], tMax3[2]));

  Real tNear = std::max(std::max(tNear3[0], tNear3[1]), tNear3[2]);
  Real tFar  = std::min(std::min( tFar3[0],  tFar3[1]),  tFar3[2]);

  bool hit = tNear <= tFar;
  return hit && tMax >= tNear && tMin <= tFar;
} // end intersectBox()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  template<typename Intersector>
    bool bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::intersect(const Point &o, const Point &d,
                  Real tMin, Real tMax,
                  Intersector &intersect) const
{
  Point invDir;
  invDir[0] = Real(1.0) / d[0];
  invDir[1] = Real(1.0) / d[1];
  invDir[2] = Real(1.0) / d[2];

  NodeIndex currentNode = mRootIndex;
  NodeIndex hitIndex;
  NodeIndex missIndex;
  bool hit = false;
  bool result = false;
  float t = tMax;
  while(currentNode != NULL_NODE)
  {
    hitIndex = nodes_[currentNode].hit_index_;
    missIndex = nodes_[currentNode].miss_index_;

    // leaves (primitives) are listed before interior nodes
    // so bounding boxes occur after the root index
    if(currentNode >= mRootIndex)
    {
      hit = intersectBox(o, invDir,
                         nodes_[currentNode].min_corner_,
                         nodes_[currentNode].max_corner_,
                         tMin, tMax);
    } // end if
    else
    {
      // XXX we can potentially ignore the checks on tMax and tMin
      //     here if we require the Intersector to do it
      hit = intersect(o,d,currentNode,t) && t < tMax && t > tMin;
      result |= hit;
      if(hit)
        tMax = std::min(t, tMax);
    } // end else

    currentNode = hit ? hitIndex : missIndex;
  } // end while

  return result;
} // end bounding_volume_hierarchy::intersect()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  template<typename Bounder>
    void bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::findBounds(const std::vector<size_t>::iterator begin,
                   const std::vector<size_t>::iterator end,
                   const std::vector<Primitive> &primitives,
                   CachedBounder<Bounder> &bound,
                   Point &m, Point &M)
{
  Real inf = std::numeric_limits<Real>::infinity();
  m = Point( inf,  inf,  inf);
  M = Point(-inf, -inf, -inf);

  Real x;
      
  for(std::vector<size_t>::iterator t = begin;
      t != end;
      ++t)
  {
    for(size_t i =0;
        i < 3;
        ++i)
    {
      x = bound(i, true, *t);

      if(x < m[i])
      {
        m[i] = x;
      } // end if

      x = bound(i, false, *t);

      if(x > M[i])
      {
        M[i] = x;
      } // end if
    } // end for j
  } // end for t

  // always widen the bounding box
  // this ensures that axis-aligned primitives always
  // lie strictly within the bounding box
  for(size_t i = 0; i != 3; ++i)
  {
    m[i] -= EPS;
    M[i] += EPS;
  } // end for i
} // end bounding_volume_hierarchy::findBounds()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  template<typename Bounder>
    void bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::build(const std::vector<Primitive> &primitives,
              Bounder &bound)
{
  nodes_.clear();

  // we will sort an array of indices
  std::vector<size_t> primIndices(primitives.size());
  for(size_t i = 0; i != primitives.size(); ++i)
  {
    primIndices[i] = i;
  } // end for i

  // initialize
  // Leaf nodes come at the beginning of the list of nodes
  // Create as many as we have primitives
  for(size_t i = 0; i != primitives.size(); ++i)
  {
    // we don't know the leaf node's parent at first,
    // so set it to NULL_NODE for now
    addNode(NULL_NODE);
  } // end for i

  // create a CachedBounder
  CachedBounder<Bounder> cachedBound(bound,primitives);

  // recurse
  mRootIndex = build(NULL_NODE,
                     primIndices.begin(),
                     primIndices.end(),
                     primitives,
                     cachedBound);

  assert(nodes_.size() == 2 * primitives.size() - 1);

  // for each node, compute the index of the
  // next node in a hit/miss ray traversal
  NodeIndex miss,hit;
  for(int i = 0; i < nodes_.size(); ++i)
  {
    if(nodes_[i].parent_index_ == NULL_NODE && i != mRootIndex)
    {
      assert(0);
    }

    hit = computeHitIndex(i);
    miss = computeMissIndex(i);
    nodes_[i].hit_index_ = hit;
    nodes_[i].miss_index_ = miss;
  }
}


template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  template<typename Bounder>
    typename bounding_volume_hierarchy<PrimitiveType, PointType, RealType>::NodeIndex
      bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
        ::build(const NodeIndex parent,
                std::vector<size_t>::iterator begin,
                std::vector<size_t>::iterator end,
                const std::vector<PrimitiveType> &primitives,
                Bounder &bound)
{
  if(begin + 1 == end)
  {
    // we've hit a leaf
    NodeIndex result = *begin;

    // set its relatives
    nodes_[result].parent_index_ = parent;
    nodes_[result].left_child_index_ = NULL_NODE;
    nodes_[result].right_child_index_ = NULL_NODE;

    return result;
  } // end if
  else if(begin == end)
  {
    std::cerr << "bounding_volume_hierarchy::build(): empty base case." << std::endl;
    return NULL_NODE;
  } // end else if
  
  // find the bounds of the Primitives
  Point m, M;
  findBounds(begin, end, primitives, bound, m, M);

  // create a new node
  NodeIndex index = addNode(parent);
  nodes_[index].min_corner_ = m;
  nodes_[index].max_corner_ = M;
  
  size_t axis = findPrincipalAxis(m, M);

  // create an ordering
  PrimitiveSorter<Bounder> sorter(axis,primitives,bound);
  
  // sort the median
  std::vector<size_t>::iterator split
    = begin + (end - begin) / 2;

  std::nth_element(begin, split, end, sorter);

  NodeIndex leftChild = build(index, begin, split, primitives, bound);
  NodeIndex rightChild = build(index, split, end, primitives, bound);

  nodes_[index].left_child_index_  = leftChild;
  nodes_[index].right_child_index_ = rightChild;
  nodes_[index].miss_index_ = NULL_NODE;

  return index;
} // end bounding_volume_hierarchy::build()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  size_t bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
    ::findPrincipalAxis(const Point &min,
                        const Point &max)
{
  // find the principal axis of the points
  size_t axis = 4;
  float maxLength = -std::numeric_limits<Real>::infinity();
  float temp;
  for(size_t i = 0; i < 3; ++i)
  {
    temp = max[i] - min[i];
    if(temp > maxLength)
    {
      maxLength = temp;
      axis = i;
    } // end if
  } // end for

  return axis;
} // end bounding_volume_hierarchy::findPrincipalAxis()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  typename bounding_volume_hierarchy<PrimitiveType, PointType, RealType>::NodeIndex
    bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::computeHitIndex(const NodeIndex i) const
{
  // case 1
  // return the left index if we are an interior node
  NodeIndex result = nodes_[i].left_child_index_;
  
  if(result == NULL_NODE)
  {
    // case 1
    // the next node to visit after a hit is our right brother, 
    // if he exists
    result = computeRightBrotherIndex(i);
    if(result == NULL_NODE)
    {
      // if we have no right brother, return my parent's
      // miss index
      result = computeMissIndex(nodes_[i].parent_index_);
    } // end if
  } // end if

  return result;
} // end bounding_volume_hierarchy::computeMissIndex()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  typename bounding_volume_hierarchy<PrimitiveType, PointType, RealType>::NodeIndex
    bounding_volume_hierarchy<PrimitiveType, PointType, RealType>
      ::computeMissIndex(const NodeIndex i) const
{
  NodeIndex result = mRootIndex;
  
  // case 1
  // there is no next node to visit after the root
  if(i == mRootIndex)
  {
    result = NULL_NODE;
  } // end if
  else
  {
    // case 2
    // if i am my parent's left child, return my brother
    result = computeRightBrotherIndex(i);
    if(result == NULL_NODE)
    {
      // case 3
      // return my parent's miss index
      result = computeMissIndex(nodes_[i].parent_index_);
    } // end if
  } // end else

  return result;
} // end bounding_volume_hierarchy::computeMissIndex()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  typename bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::NodeIndex
    bounding_volume_hierarchy<PrimitiveType,PointType,RealType>
      ::computeRightBrotherIndex(const NodeIndex i) const
{
  NodeIndex result = NULL_NODE;
  NodeIndex parent = nodes_[i].parent_index_;

  if(i == nodes_[parent].left_child_index_)
  {
    result = nodes_[parent].right_child_index_;
  } // end if

  return result;
} // bounding_volume_hierarchy::computeRightBrotherIndex()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  typename bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::NodeIndex
    bounding_volume_hierarchy<PrimitiveType,PointType,RealType>
      ::getRootIndex(void) const
{
  return mRootIndex;
} // end bounding_volume_hierarchy::getRootIndex()

template<typename PrimitiveType,
         typename PointType,
         typename RealType>
  template<typename Bounder>
    bounding_volume_hierarchy<PrimitiveType,PointType,RealType>::CachedBounder<Bounder>
      ::CachedBounder(Bounder &bound,
                      const std::vector<Primitive> &primitives)
{
  mPrimMinBounds[0].resize(primitives.size());
  mPrimMinBounds[1].resize(primitives.size());
  mPrimMinBounds[2].resize(primitives.size());

  mPrimMaxBounds[0].resize(primitives.size());
  mPrimMaxBounds[1].resize(primitives.size());
  mPrimMaxBounds[2].resize(primitives.size());

  size_t i = 0;
  for(typename std::vector<PrimitiveType>::const_iterator prim = primitives.begin();
      prim != primitives.end();
      ++prim, ++i)
  {
    mPrimMinBounds[0][i] = bound(0, true, *prim);
    mPrimMinBounds[1][i] = bound(1, true, *prim);
    mPrimMinBounds[2][i] = bound(2, true, *prim);

    mPrimMaxBounds[0][i] = bound(0, false, *prim);
    mPrimMaxBounds[1][i] = bound(1, false, *prim);
    mPrimMaxBounds[2][i] = bound(2, false, *prim);
  } // end for prim
} // end CachedBounder::CachedBounder()

