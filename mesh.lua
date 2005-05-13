require('bokstd')

blob = gfx.texture('blob.png')


t = tracker.new(50, 200)

m = mesh.new()

-- Edges which aren't in the mesh.  Indexed by endpoints, 
-- so that we can find and recycle an edge here.
nonmeshedges = {}
nme_meta = {
   add = function (self, e)
	    self[e] = e
	    self[e:key()] = e
	 end,
   remove = function (self, e)
	       --print('removing',self[e],self[tostring(e)])
	       self[e] = nil
	       self[e:key()] = nil
	    end,
   find = function(self, v1, v2)
	     return self[edgekey(v1,v2)]
	  end
}
nme_meta.__index = nme_meta
setmetatable(nonmeshedges, nme_meta)

-- Edges indexed by endpoint.
-- Each vertex has multiple edges (at least 2, for a triangulated mesh), 
-- so each one has a set of edges associated with it.
edgebyvert = {}
function edgebyvert:add(e)
   --print('add ', e[1], e[2])
   local function ins(v)
      -- insert a vertex, creating a new set if it did't exist
      if not self[v] then
	 self[v] = {}
      end
      self[v][e] = e		-- add edge to set
   end
   ins(e[1])
   ins(e[2])
end
function edgebyvert:remove(v)
   -- vertex may not have edges yet
   if not self[v] then
      return
   end

   for _,e in self[v] do	--for each edge in set
      --remove the other end
      self[e[1]][e] = nil
      self[e[2]][e] = nil
      nonmeshedges:remove(e)	--remove from nonmeshedges
   end
   self[v] = nil		--remove this vertex
end


edgemeta={
   age = 20
}
edgemeta.__index = edgemeta

function edgemeta:remove()
   nonmeshedges:add(self)
end

--use invarient keys of endpoints to build a key for the edge
function edgekey(v1, v2)   
   local p1,p2 = v1,v2
   if p1.key > p2.key then
      p1,p2 = p2,p1
   end

   local ret= p1.key .. '-' .. p2.key
   --print ('edgekey', ret)
   return ret
end

function edgemeta:key()
   return edgekey(self[1], self[2])
end

function edgemeta:__tostring()
   return string.format('%s - %s', tostring(self[1]), tostring(self[2]))
end

-- Called to create a new mesh edge.  Check the nonmeshedges structure
-- for an edge to recycle before deciding to create a new one, so that
-- any per-edge state is preserved.
function m:newedge(v1, v2)
   --print('v1:', v1, 'v2: ',v2)

   e = nonmeshedges:find(v1, v2)
   if e then
      --print('recycling', e)
      nonmeshedges:remove(e)
   else
      e = {v1,v2}
      setmetatable(e, edgemeta)

      edgebyvert:add(e)
      --print('creating',e)
   end

   e.age = edgemeta.age

   return e
end

--metatable for points
pointmeta = {}
pointmeta.__index = pointmeta
function pointmeta:lost(why)
   m:del(self)
   edgebyvert:remove(self)
end
function pointmeta:draw()
   gfx.setstate{colour=self.colour}
   gfx.sprite(self, 5, blob)
end
function pointmeta:move(x, y)
   table.insert(self.history, 1, {x=self.x, y=self.y})
   table.remove(self.history)

   local dx,dy
   dx = self.x - x
   dy = self.y - y
   self.x,self.y = x,y
   m:move(self)
end
function pointmeta:__tostring()
   return string.format('%g,%g', self.x, self.y)
end
function pointmeta.__lt(a,b)
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

--add a new tracked point to the features set
function features:add(idx, x, y, weight)
   pt = { x=x, y=y, key='pt'..unique() }

   local h = {}
   table.setn(h, 30)
   pt.history = h

   pt.colour = {r=math.random(), g=math.random(), b=math.random()}

   setmetatable(pt, pointmeta)

   self[idx] = pt
   m:add(pt)
   --print('add pt.__mesh=',pt.__mesh)
end

function process_frame(frame)
   --print('Frame!', frame)

   t:track(features)

   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}
   features:foreach('draw')

   gfx.setstate{colour={}, blend='none'}

--[[
   for e in m:edges() do
      local p1,p2 = e[1], e[2]
      --print('p1=',p1.x,p1.y, 'p2=',p2.x,p2.y)

      local b = e.age / 20
      gfx.setstate({colour={b,b,b}})
      e.age = e.age - 1
      if e.age < 0 then
	 e.age = 0
      end

      gfx.line(p1, p2)
   end
--]]

--[[
   gfx.setstate({colour={0,1,0}})
   for k,e in nonmeshedges do
      if k == e then
	 local p1,p2 = e[1], e[2]
	 --print('p1=',p1, 'p2=',p2)
	 
	 gfx.line(p1, p2)
      end
   end
--]]
--[[
   gfx.setstate{colour={1,1,0}}
   for _,t in m:triangles() do
      gfx.line(t[1], t[2], t[3], t[1])
      --print(t[1], t[2], t[3])
   end
--]]

   gfx.setstate{colour={.5,.5,.5,.5}, blend='alpha'}
   --m:draw(frame)

   --drawmemuse(frame)   

   --print('gcinfo=', gcinfo())
end
