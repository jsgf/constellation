--[[

Cluster tracked features into objects.

First step is to group tracked features into triangles.  If a triangle
retains consistent angles over time, it means the features are
probably attached to the same thing; this is a "stable" triangle.  The
angles will be invarient under translation, rotation and scaling.  NB:
this is not a planar trianglular mesh: *every* point is in a triangle
with every other.

Second step is to group triangles into objects.  If a triangle is
stable, and it shares an edge (two points) with another stable
triangle, they are grouped into one object.  This proceeds until all
stable triangles are part of some object.


Overall structure:

points -> edges
edges -> triangles
triangles are members of clusters

]]

require('bokstd')

if false then
   debug=print
else
   function debug(...)
   end
end

blob = gfx.texture('blob.png')

--t = tracker.new(50, 200)
--t = tracker.new(20, 20)
t = tracker.new(10,10)

--metatable for points
meta_point = {}
meta_point.__index = meta_point

-- tracked point has been lost; clean up everything attached to it
function meta_point:lost(why)
   --print('lost',self,why)

   for _,e in self.edges do
      e:del()			--unlink
   end
end

function meta_point:draw()
   gfx.setstate{colour=self.colour}
   gfx.sprite(self, 5, blob)
end

function meta_point:move(x, y)
   local dx,dy

   dx = self.x - x
   dy = self.y - y

   self.x,self.y = x,y
   self.dx = dx
   self.dy = dy
end

function meta_point:update()
end

function meta_point:__tostring()
   return string.format('%g,%g', self.x, self.y)
end

function meta_point.__lt(a,b)
   if a.y == b.y then
      if a.x < b.x then
	 return a
      else
	 return b
      end
   else if a.y < b.y then
	 return a
      else
	 return b
      end
   end
end

-- get an edge between self and pt, creating it if necessary.  Each edge has 
-- a set of triangles which use that edge.
function meta_point:edge(pt)
   local e = self.edges[pt]

   if e then
      assert(e == pt.edges[self])
   else
      e = { self, pt, tris={} }

      setmetatable(e, meta_edge)

      self.edges[pt] = e
      pt.edges[self] = e

      --print('new edge', tostring(e))
   end

   return e
end

-- add a new tracked point to the features set
function features:add(idx, x, y, weight)
   pt = { x=x, y=y, key='pt'..unique(), edges={} }

   pt.colour = { r=math.random(), g=math.random(), b=math.random() }

   setmetatable(pt, meta_point)

   self[idx] = pt

   -- construct all the triangles with this point
   --print('new point', pt.key, tostring(pt))
   for _,p1 in self:points() do
      for _,p2 in self:points() do
	 if pt ~= p1 and pt ~= p2 and p1 ~= p2 then
	    triangles:new(p1, p2, pt)
	 end
      end
   end
end

-- -- -- Edges
meta_edge={}
meta_edge.__index = meta_edge

-- add a triangle to an edge
function meta_edge:addtri(tri)
   self.tris[tri] = tri
   --print('added tri: ', tostring(self))
end

-- remove triangle from edge
function meta_edge:deltri(tri)
   self.tris[tri] = nil
end

-- unlink edge from endpoints and triangles
function meta_edge:del()
   self[1].edges[self[2]] = nil
   self[2].edges[self[1]] = nil

   for _,t in self.tris do
      triangles:del(t)	--remove triangles
   end
end

function meta_edge:__tostring()
   local ret = string.format('%s-%s: ', self[1].key, self[2].key)
   for t in self.tris do
      ret = ret..string.format('%s; ', t:key())
   end

   return ret
end

-- --

-- Set of all existing triangles.  This table is a set of triangles (tri->tri)
triangles={}

--[[ Triangles
In addition to their methods, triangles have the following data:

[1,2,3]	- points, as integer indices
angles[1,2,3] - initial angles of the corners

]]

vector={}
function vector.dot(a,b)
   return a.x*b.x + a.y*b.y
end
function vector.sub(a,b)
   return {x=b.x-a.x, y=b.y-a.y}
end
function vector.norm(a)
   return math.sqrt(a.x*a.x + a.y*a.y)
end
function vector.normalize(a)
   local l = vector.norm(a)
   return {x=a.x/l, y=a.y/l}
end
function vector.angle(a)
   return math.atan2(a.y, a.x)
end

-- return the angle at corner ABC, where b is the corner
function angle(a,b,c)
   return vector.angle(vector.sub(b,a))-vector.angle(vector.sub(b,c))
end

meta_triangle = {}
meta_triangle.__index = meta_triangle

-- Return a scalar representing how much the angles have changed since 
-- the triangle was created.
-- XXX Maybe this should factor in the lengths
-- of the edges so that angular changes between long edges are more
-- significant than the angle between short edges...
function meta_triangle:diff()
   local angles = {
      angle(self[2], self[1], self[3]),
      angle(self[3], self[2], self[1]),
      angle(self[1], self[3], self[2]),
   }

   local c1, c2, c3
   c1 = math.cos(angles[1] - self.angles[1])
   c2 = math.cos(angles[2] - self.angles[2])
   c3 = math.cos(angles[3] - self.angles[3])

   return c1*c2*c3
end

-- returns true if a triangle seems to be the same shape as it was when created
function meta_triangle:stable()
   return self:diff() > .999
end

function meta_triangle:__tostring()
   return string.format('(%s; %s; %s)', 
			tostring(self[1]), 
			tostring(self[2]), 
			tostring(self[3]))
end

function meta_triangle:draw()
   gfx.line(self[1], self[2], self[3], self[1])
end

function meta_triangle:key()
   return string.format('%s:%s:%s', self[1].key, self[2].key, self[3].key)
end

function meta_triangle:edges()
   return { 
      self[1]:edge(self[2]),
      self[2]:edge(self[3]),
      self[3]:edge(self[1]) }
end

-- visit all the triangles sharing an edge with this one, 
-- and all their neighbours recursively, returning the 
-- ones which match pred.
function meta_triangle:neighbours(pred)
   local visited={}
   local ret={}

   local function _neighbours(tri)
      for _,e in tri:edges() do
	 for t in e.tris do
	    if not visited[t] then
	       visited[t] = t
	       if pred(t) then
		  ret[t] = t
		  _neighbours(t)
	       end
	    end
	 end
      end
   end
   _neighbours(self)

   return ret
end

-- remove self from edges
function meta_triangle:remove()
   self[1]:edge(self[2]):deltri(self)
   self[2]:edge(self[3]):deltri(self)
   self[3]:edge(self[1]):deltri(self)
end

-- create a new triangle from p1,p2,p3
-- points are sorted into a canonical order
function triangles:new(p1, p2, p3)
   local tri = { p1, p2, p3 }

   setmetatable(tri, meta_triangle)

   -- sort points into canonical form
   table.sort(tri, function(a, b) return a.key < b.key end)

   if self[tri:key()] then
      return
   end
   --print('new triangle', tri:key())

   tri.angles = {
      angle(tri[2], tri[1], tri[3]),
      angle(tri[3], tri[2], tri[1]),
      angle(tri[1], tri[3], tri[2]),
   }

   --print('dots',tri:key(),tri.dots[1],tri.dots[2],tri.dots[3])

   self[tri:key()] = tri

   -- add triangle to edges
   p1:edge(p2):addtri(tri)
   p2:edge(p3):addtri(tri)
   p3:edge(p1):addtri(tri)

   --print(tri:key())
end

-- remove a triangle from triangle set
function triangles:del(tri)
   tri:remove()
   triangles[tri:key()] = nil
end

-- --
function dot(a, b)
   return a.x*b.x + a.y*b.y
end

meta_cluster={}
meta_cluster.__index = meta_cluster

-- add a triangle to the cluster
function meta_cluster:add(tri)   
   self.triangles[tri] = tri
end

-- remove a triangle from the cluster
function meta_cluster:remove(tri)
   self.triangles[tri] = nil
end

-- draw a cluster
function meta_cluster:draw()
   local m = mesh.new()

   if false then
      for t in self.triangles do
	 gfx.line(t[1], t[2], t[3])
      end
   else
      -- construct a mesh of all the points in this cluster
      local p = {}

      for t in self.triangles do
	 for i = 1,3 do
	    local pt = t[i]
	    if not p[pt] then
	       p[pt] = pt
	       m:add(pt)
	       pt.colour = self.colour
	    end
	 end
      end

      gfx.setstate{colour=self.colour, blend='add'}
      m:draw()

      gfx.setstate{colour={1,1,0}, blend='none'}
      for e in m:boundary() do
	 --print('edge',e[1],e[2])
	 gfx.line(e[1], e[2])
      end
   end
end

function newcluster(tri)
   local c = {}

   setmetatable(c, meta_cluster)

   c.triangles = {}
   c.colour = {r=math.random()*.5, g=math.random()*.5, b=math.random()*.5, a=.5}

   c.triangles[tri] = tri

   return c
end

-- Group triangles into clusters
function make_clusters()
   clusters={}
   local cluster_tris={}

   for _,t in filter(function (k,v) return type(v) == 'table' and v:stable() end, triangles) do
      -- t is a stable triangle
      if not cluster_tris[t] then
	 debug('new cluster for ', t:key())
	 local c = newcluster(t)

	 if not t.colour then
	    t.colour = c.colour
	 else
	    c.colour = t.colour
	 end

	 --print('t.colour=',t.colour,'c.colour=',c.colour)

	 clusters[c] = c
	 cluster_tris[t] = c
	 for nt in t:neighbours(function (k,v) 
				   return k ~= t and not cluster_tris[k] and k:stable() 
				end) do
	    -- nt is a stable neighbour which isn't already part of a cluster (though it shouldn't be)
	    debug('  adding ', nt:key())
	    c:add(nt)
	    cluster_tris[nt] = c
	    nt.colour = c.colour
	 end
      end
   end
end

function process_frame(frame)
   debug('Frame!', frame)

   t:track(features)

   gfx.setstate{colour={.75,.75,.75}, blend='none'}
   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}

   make_clusters()

   if false then
      gfx.setstate{colour={1,1,0,1}, blend='none'}
      for a,b in triangles do
	 if a==b then
	    a:draw()
	 end
      end

   else
      local c
      for c in clusters do
	 c:draw(frame)
      end
   end

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}
   for _,p in features:points() do
      gfx.sprite(p, 4, blob)
   end

   --drawmemuse(frame)   

   --print('gcinfo=', gcinfo())
end
