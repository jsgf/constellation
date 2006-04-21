require('bokstd')

blob = gfx.texture('blob.png')
track = tracker.new(50, 200)

function randomcol()
   return {
      r = .5 + math.random() * .5,
      g = .5 + math.random() * .5,
      b = .5 + math.random() * .5
   }
end

-- An individual constellation
do
   -- constellation meta table
   local _const = {}
   _const.__index = _const

   local function edge_key(p1, p2)
      assert(p1 ~= p2)

      if p1.key > p2.key then
	 p1,p2 = p2,p1
      end

      return p1.key .. '-' .. p2.key
   end

   function _const:add_star_edge(p, to)
      if self.stars[p] == nil then
	 -- print('const new star '..tostring(p))
	 self.stars[p] = { }
      end

      self.stars[p][to] = to

      -- print('const '..tostring(self)..' add edge '..edge_key(p, to))
   end

   function _const:del_edge(ek)
      assert(self.edges[ek] ~= nil)

      p1,p2 = unpack(self.edges[ek])

      assert(self.stars[p1] ~= nil)
      assert(self.stars[p2] ~= nil)

      self.stars[p1][p2] = nil
      self.stars[p2][p1] = nil

      self.edges[ek] = nil
      self.num_edges = self.num_edges - 1
   end

   function _const:del_star(p)
      -- print(self,'const del star '..tostring(p))

      assert(self.stars[p] ~= nil)

      for _,to in self.stars[p] do
	 local ek = edge_key(p, to)

	 -- print('  edge '..ek)
	 self:del_edge(ek)
      end

      self.stars[p] = nil
   end

   function _const:add_edge(p1, p2)
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

   function _const:hasedge(p1, p2)
      local ek = edge_key(p1, p2)
      return self.edges[ek] ~= nil
   end

   function _const:complete()
      return self.num_edges >= 4
   end

   function _const:empty()
      return self.num_edges == 0
   end

   function _const:draw()
      gfx.setstate{colour = self.colour}
      for _,e in self.edges do
	 --[[
	 self:valid_star(e[1])
	 self:valid_star(e[2])
	 --]]
	 gfx.line(unpack(e))
      end
   end

   function _const:valid_star(pt)
      print('testing star '..tostring(pt)..' in const '..tostring(self))

      assert(self.stars[pt] ~= nil)
      assert(self.vault.conststars[pt] == self)
   end

   function constellation(vault, name)
      ret = {
	 vault = vault,
	 num_edges = 0,
	 edges = {},
	 stars = {},
	 name = name,
	 colour = randomcol(),
      }

      setmetatable(ret, _const)
      return ret
   end
end

-- Heavens.  This keeps track of all the stars which are part of
-- constellations
do
   -- heavens metatable
   local _heavens = {}
   _heavens.__index = _heavens

   function _heavens:add_freestar(pt)
      assert(self.freestars[pt] == nil)
      assert(pt._freestar_idx == nil)

      self.freestars[pt] = pt
   end

   function _heavens:del_freestar(pt)
      --print('removing free ' .. tostring(pt))
      assert(self.freestars[pt] ~= nil)

      self.freestars[pt] = nil
   end

   function _heavens:add_star(pt)
      --print('adding star ' .. tostring(pt))
      assert(self.stars[pt] == nil)
      self.stars[pt] = pt

      self:add_freestar(pt)
   end

   function _heavens:del_star(pt)
      --print('removing star ' .. tostring(pt))
      assert(self.stars[pt] ~= nil)

      -- remove from any constellations
      local c = self.conststars[pt]
      self.conststars[pt] = nil

      if c ~= nil then
	 c:del_star(pt)
      else
	 self:del_freestar(pt)
      end
      
      self.stars[pt] = nil
   end

   function neighbour_set(m, star)
      local ret = {}

      for _,p in m:connected(star) do
	 -- print('  ' .. tostring(star) .. ' -> ' .. tostring(p))
	 table.insert(ret, p)
      end

      return ret
   end

   -- Build a new constellation
   function _heavens:make_constellation()
      -- Build a nicely indexable table of free stars
      local free = {}
      for _,s in self.freestars do
	 table.insert(free, s)
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
      assert(self.conststars[star] == nil)

      local const = constellation(self)
      local starset = {}

      starset[star] = star

      -- print('new const')
      while not const:complete() or math.random() < .2 do
	 -- print('  star '..tostring(star))

	 local neighbours = neighbour_set(m, star)
	 local next = nil

	 -- find the next step
	 while table.getn(neighbours) > 0 do
	    local idx = math.random(table.getn(neighbours))
	    next = neighbours[idx]

	    -- check to make sure the star isn't already part of an existing
	    -- constellation and it isn't a double-back within the current one
	    if self.conststars[next] == nil and
	       not const:hasedge(star, next) then
	       break		-- OK, use this one
	    end
	    next = nil
	    table.remove(neighbours, idx)
	 end

	 if next == nil then
	    -- failed to find a suitable next star
	    break
	 end

	 const:add_edge(star, next)
	 star = next
	 starset[star] = star
      end

      -- if we were successful, add stars to constellation map
      if const:complete() then
	 for _,s in starset do
	    assert(self.conststars[s] == nil)
	    assert(self.freestars[s] ~= nil)

	    -- print('added star '..tostring(s)..' to const '..tostring(const))

	    self.conststars[s] = const
	    self.const[const] = const
	    self:del_freestar(s)
	 end
      end

      -- clean up
      m:del(unpack(m:points()))
   end

   function _heavens:draw()
      table.foreach(self.const,
		    function (_,c)
		       c:draw()
		    end)
   end

   function heavens()
      h = {
	 const = {},		-- constellations (set)
	 conststars = {},	-- star -> const (map)

	 stars = {},		-- all stars (set)
	 freestars = {},	-- uncommitted stars (set)
      }

      setmetatable(h, _heavens)

      return h
   end
end


-- Point class
do
   local _point = {}
   _point.__index = _point

   function _point:lost(why)
      if self.state == 'mature' then
	 vault:del_star(self)
      end
   end

   function _point:draw()
      gfx.setstate{colour = self.colour}
      gfx.sprite(self, 5, blob)
   end

   function _point:move(x, y)
      self.x, self.y = x,y
   end

   function _point:update()
      self.age = self.age + 1
      if self.state == 'new' and self.age > 5 then
	 self.state = 'mature'
	 vault:add_star(self)
      end
   end

   function _point:__tostring()
      --return string.format('%g,%g', self.x, self.y)
      return self.key
   end

   function _point:__lt(a,b)
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
      pt.colour = randomcol()
      setmetatable(pt, _point)

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

   gfx.setstate{colour={.5,.5,.5,.5}, blend='none'}
   drawframe(frame)

   gfx.setstate{colour={1,1,0,1}, blend='alpha'}
   features:foreach('update')


   vault:make_constellation()

   vault:draw()
   features:foreach('draw')

   drawmemuse(frame)   
end