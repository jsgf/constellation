require('bokstd')

blob = gfx.texture('blob.png')
track = tracker.new(50, 200)

-- A specific constellation
do
   local meta = {}
   meta.__index = meta

   local function edge_key(p1, p2)
      assert(p1 ~= p2)

      if p1.key > p2.key then
	 p1,p2 = p2,p1
      end

      return p1.key .. '-' .. p2.key
   end

   function meta:add_star_edge(p, to)
      --print('const add star '..tostring(p)..' -> '..tostring(to))
      if self.stars[p] == nil then
	 --print('  new star')
	 self.stars[p] = { }
      end
      self.stars[p][to] = to
   end

   function meta:del_edge(ek)
      --print('  remove edge '..ek)

      assert(self.edges[ek] ~= nil)

      p1,p2 = unpack(self.edges[ek])

      --print(p1,p2)

      --[[
      for s,et in self.stars do
	 print('  from ' .. tostring(s) .. ' -> ')
	 for to,_ in et do
	    print('    '..tostring(to))
	 end
      end
      --]]
      assert(self.stars[p1] ~= nil)
      assert(self.stars[p2] ~= nil)

      self.stars[p1][p2] = nil
      self.stars[p2][p1] = nil

      self.edges[ek] = nil
      self.num_edges = self.num_edges - 1
   end

   function meta:del_star(p)
      --print(self,'const del star '..tostring(p))

      assert(self.stars[p] ~= nil)

      for _,to in self.stars[p] do
	 local ek = edge_key(p, to)

	 --print('  edge '..ek)
	 self:del_edge(ek)
      end

      self.stars[p] = nil
   end

   function meta:add_edge(p1, p2)
      local ek = edge_key(p1, p2)

      if self.edges[ek] ~= nil then
	 return
      end

      --print('add edge '..ek)
      self.num_edges = self.num_edges + 1
      self.edges[ek] = ({ p1, p2 })

      self:add_star_edge(p1, p2)
      self:add_star_edge(p2, p1)
   end

   function meta:hasedge(p1, p2)
      local ek = edge_key(p1, p2)
      return self.edges[ek] ~= nil
   end

   function meta:complete()
      return self.num_edges >= 4
   end

   function meta:draw()
      for _,e in self.edges do
	 gfx.line(unpack(e))
      end
   end

   function constellation(name)
      ret = {
	 num_edges = 0,
	 edges = {},
	 stars = {},
	 name = name,
      }

      setmetatable(ret, meta)
      return ret
   end
end

-- Heavens.  This keeps track of all the stars which are part of
-- constellations
do
   local meta = {}
   meta.__index = meta

   function meta:add_freestar(pt)
      assert(self.freestars[pt] == nil)
      assert(pt._freestar_idx == nil)

      self.freestars[pt] = pt
   end

   function meta:del_freestar(pt)
      --print('removing free ' .. tostring(pt))
      assert(self.freestars[pt] ~= nil)

      self.freestars[pt] = nil
   end

   function meta:add_star(pt)
      --print('adding star ' .. tostring(pt))
      assert(self.stars[pt] == nil)
      self.stars[pt] = pt

      self:add_freestar(pt)
   end

   function meta:del_star(pt)
      --print('removing star ' .. tostring(pt))
      assert(self.stars[pt] ~= nil)

      -- remove from any constellations
      if self.const[pt] ~= nil then
	 self.const[pt]:del_star(pt)
      end
      
      -- remove from other star sets
      self.stars[pt] = nil

      if self.freestars[pt] ~= nil then
	 self:del_freestar(pt)
      end
   end

   function neighbour_set(m, star)
      local ret = {}

      for _,p in m:connected(star) do
	 -- print('  ' .. tostring(star) .. ' -> ' .. tostring(p))
	 table.insert(ret, p)
      end

      return ret
   end

   function meta:make_constellation()
      -- Build a nicely indexable table of free stars
      local free = {}
      for _,v in self.freestars do
	 table.insert(free, v)
      end

      if table.getn(free) < 20 then
	 -- not enough to get started
	 return
      end

      -- first, create a mesh for all the stars
      local m = mesh.new()

      for _,p in self.stars do
	 m:add(p)
      end

      -- find an initial star
      local star = free[math.random(table.getn(free))]

      assert(self.stars[star] ~= nil)

      local const = constellation()
      local conststars = {}

      --print('new const')
      while not const:complete() or math.random() < .2 do
	 conststars[star] = star

	 local neighbours = neighbour_set(m, star)
	 local next = nil

	 -- find the next step
	 while table.getn(neighbours) > 0 do
	    local idx = math.random(table.getn(neighbours))
	    next = neighbours[idx]

	    -- check to make sure the star isn't already part of an existing
	    -- constellation and it isn't a double-back within the current one
	    if self.const[next] == nil and not const:hasedge(star, next) then
	       break		-- OK, use this one
	    end
	    next = nil
	    table.remove(neighbours, idx)
	 end

	 if next == nil then
	    -- failed to find a suitable next star
	    break
	 end

	 --print('  edge('.. tostring(star) ..' -> '.. tostring(next) ..')')
	 const:add_edge(star, next)
	 star = next
      end

      -- if we were successful, add stars to constellation map
      if const:complete() then
	 for _,s in conststars do
	    self.const[s] = const
	    table.insert(self.const, const)
	    self:del_freestar(s)
	 end
      end

      -- clean up
      for _,p in self.stars do
	 m:del(p)
      end
   end

   function meta:draw()
      table.foreachi(self.const, function (_,c) c:draw() end)
   end

   function heavens()
      h = {
	 const = {},		-- constellations
	 stars = {},		-- all stars
	 freestars = {},	-- uncommitted stars
      }

      setmetatable(h, meta)

      return h
   end
end


-- Point class
do
   local meta = {}
   meta.__index = meta

   function meta:lost(why)
      if self.state == 'mature' then
	 vault:del_star(self)
      end
   end

   function meta:draw()
      gfx.setstate{colour = self.colour}
      gfx.sprite(self, 5, blob)
   end

   function meta:move(x, y)
      self.x, self.y = x,y
   end

   function meta:update()
      self.age = self.age + 1
      if self.state == 'new' and self.age > 10 then
	 self.state = 'mature'
	 vault:add_star(self)
      end
   end

   function meta:__tostring()
      --return string.format('%g,%g', self.x, self.y)
      return self.key
   end

   function meta:__lt(a,b)
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

   -- Constructor
   function point(x,y)
      pt = {
	 x = x,
	 y = y,
	 key = 'pt'..unique(),
	 age = 0,
	 state = 'new'
      }
      pt.colour = {
	 r = .5 + math.random() * .5,
	 g = .5 + math.random() * .5,
	 b = .5 + math.random() * .5
      }
      setmetatable(pt, meta)

      return pt
   end
end

--add a new tracked point to the features set
function features:add(idx, x, y, weight)
   pt = point(x, y)

   self[idx] = pt

   --print('add pt.__mesh=',pt.__mesh)
end

vault = heavens()

function process_frame(frame)
   --print('Frame!', frame)

   track:track(features)

   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}
   features:foreach('update')
   features:foreach('draw')

   vault:make_constellation()

   gfx.setstate{colour={1,1,0,1}, blend='none'}
   vault:draw()

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
