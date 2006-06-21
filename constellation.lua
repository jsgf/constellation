require('bokstd')

blob = gfx.texture('star-la.png')

font = text.face('/usr/share/fonts/bitstream-vera/VeraBd.ttf', 12)
names = {}
for name in io.lines('names.txt') do
   table.insert(names, name)
end

track = tracker.new(50, 200)

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

      for _,to in pairs(self.stars[p]) do
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

   function _const:update_name(tx, ty, angle)
      local dx = tx - self.namepos.x
      local dy = ty - self.namepos.y
      local da = angle - self.nameangle

      self.namepos.x = self.namepos.x + dx * .1
      self.namepos.y = self.namepos.y + dy * .1
      self.nameangle = self.nameangle + da * .1

      font:draw(self.namepos, self.nameangle, self.name)
   end

   function _const:draw()
      local vx, vy = 0, 0
      local mx, my = 0, 0
      local count = 0

      for _,e in pairs(self.edges) do
	 --[[
	 self:valid_star(e[1])
	 self:valid_star(e[2])
	 --]]
	 local p1, p2 = unpack(e)

	 gfx.line(p1, p2)

	 mx = mx + p1.x + p2.x
	 my = my + p1.y + p2.y

	 count = count+2

	 if p1.x < p2.x then
	    vx = vx - (p1.x - p2.x)
	    vy = vy - (p1.y - p2.y)
	 else
	    vx = vx + (p1.x - p2.x)
	    vy = vy + (p1.y - p2.y)
	 end
      end

      if count ~= 0 then
	 mx = mx / count
	 my = my / count
      end

      local angle = math.atan2(vy, vx) * 180 / math.pi

      --self:update_name(mx, my, angle)
   end

   function _const:valid_star(pt)
      print('testing star '..tostring(pt)..' in const '..tostring(self))

      assert(self.stars[pt] ~= nil)
      assert(self.vault.conststars[pt] == self)
   end

   function constellation(vault, name)
      local ret = {
	 vault = vault,
	 num_edges = 0,
	 edges = {},
	 stars = {},

	 name = name,
	 namepos = { x = math.random(640)-320+160, y = math.random(480)-240+120 },
	 nameangle = math.random(360),
      }

      -- print(ret.name)
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

	 if not c:complete() then
	    self:del_const(c)
	 end
      else
	 self:del_freestar(pt)
      end
      
      self.stars[pt] = nil
   end

   local used_names = {}

   function _heavens:del_const(c)
      assert(self.const[c] == c)
      self.const[c] = nil

      used_names[c.name] = nil

      for s,_ in pairs(c.stars) do
	 assert(self.conststars[s] == c)
	 self.conststars[s] = nil
	 self:add_freestar(s)
      end
   end

   function _heavens:add_const(c)
      assert(self.const[c] == nil)
      self.const[c] = c

      used_names[c.name] = true

      for s,_ in pairs(c.stars) do
	 self.conststars[s] = c
	 self:del_freestar(s)
      end
   end

   local function unique_name()
      local ret
      repeat
	 ret = names[math.random(table.getn(names))]
      until used_names[ret] == nil

      return ret
   end

   function neighbour_set(m, star)
      local ret = {}

      for _,p in pairs(m:connected(star)) do
	 -- print('  ' .. tostring(star) .. ' -> ' .. tostring(p))
	 table.insert(ret, p)
      end

      return ret
   end

   local function next_neighbour(nset)
      local ret = nil

      if table.getn(nset) > 0 then
	 local idx = math.random(table.getn(nset))
	 ret = nset[idx]
	 table.remove(nset, idx)
      end

      return ret
   end

   -- Build a new constellation
   function _heavens:make_constellation()
      -- Build a nicely indexable table of free stars
      local free = {}
      for _,s in pairs(self.freestars) do
	 table.insert(free, s)
      end

      if table.getn(free) < 20 then
	 -- not enough to get started
	 return
      end

      -- first, create a mesh for all the stars
      local m = mesh.new()

      for _,p in pairs(self.stars) do
	 m:add(p)
      end

      -- find an initial star
      local star = free[math.random(table.getn(free))]

      assert(self.stars[star] ~= nil)
      assert(self.conststars[star] == nil)

      local const = constellation(self, unique_name())

      -- print('new const')
      while not const:complete() or math.random() < .2 do
	 -- print('  star '..tostring(star))

	 local neighbours = neighbour_set(m, star)
	 local next = nil

	 -- find the next step
	 repeat
	    next = next_neighbour(neighbours)

	    -- check to make sure the star isn't already part of an existing
	    -- constellation and it isn't a double-back within the current one
	 until next == nil or (self.conststars[next] == nil and
			       not const:hasedge(star, next))

	 if next == nil then
	    -- failed to find a suitable next star
	    break
	 end

	 const:add_edge(star, next)
	 star = next
      end

      -- if we were successful, add stars to constellation map
      if const:complete() then
	 self:add_const(const)
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

   function _heavens:cull(ratio)
      table.foreach(self.const,
		    function (_,c)
		       if math.random() < ratio then
			  self:del_const(c)
		       end
		    end)
   end

   function heavens()
      local h = {
	 const = {},		-- constellations (set)
	 conststars = {},	-- star -> const (map)

	 stars = {},		-- all stars (set)
	 freestars = {},	-- uncommitted stars (set)
      }

      setmetatable(h, _heavens)

      return h
   end
end

avg = { }

-- Point class (really a star)
do
   local _point = {}
   _point.__index = _point

   -- tunables
   local mature_age = 10
   local dying_time = 15
   local weight_limit = 100

   local state_new = { }
   local state_mature = { }
   local state_dying = { }

   function state_new.draw(self)
      local fade = self.age / mature_age
      gfx.setstate{colour = lerpcol(fade, {0,0,0,0}, self.colour)}
      gfx.sprite(self, self.size * fade, blob)
   end

   function state_new.update(self)
      self.age = self.age + 1
      if self.age >= mature_age then
	 self.state = state_mature

	 if self.weight > weight_limit then
	    vault:add_star(self)
	 end
      end
   end

   function state_new.lost(self, why)
   end

   function state_mature.draw(self)
      gfx.setstate{colour = self.colour}
      gfx.sprite(self, self.size, blob)
   end

   function state_mature.update(self)
   end

   function state_mature.lost(self, why)
      if self.weight > weight_limit then
	 vault:del_star(self)
      end

      if why ~= 'oob' then
	 self.age = 0
	 self.state = state_dying
	 
	 dying_stars[self] = self

	 self.nova = {
	    {0.,   self.colour },
	    { .3, { .792, .808, .996, .5 } },
	    { .5, { 1.00, .960, .404, .8 } },
	    { .6, { 1.00 * .6, .663 * .6, .322 * .6, .6 } },
	    { .8, { 1.00 * .5, .392 * .5, .298 * .5, .5 } },
	    { 1.,    { 0, 0, 0, 0 } },
	 }
      end
   end

   function state_dying.draw(self)
      local fade = self.age / dying_time
      
      gfx.setstate{colour = gradient(self.nova, fade)}
      gfx.sprite(self, self.size * (fade * 4 + 1), blob)
   end

   function state_dying.update(self)
      self.x = self.x + avg.dx
      self.y = self.y + avg.dy
      
      self.age = self.age + 1
      if self.age > dying_time then
	 dying_stars[self] = nil
      end
   end

   function _point:lost(why)
      self.state.lost(self, why)
   end

   function _point:move(x, y)
      avg.dx = avg.dx + x - self.x
      avg.dy = avg.dy + y - self.y
      avg.count = avg.count + 1

      self.x, self.y = x,y
   end

   function _point:update()
      self.state.update(self)
   end

   function _point:draw()
      self.state.draw(self)
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

   local main_sequence = {
      { 1,     { 1.00, .392, .298, 1. } }, -- class M
      { 200,   { 1.00, .663, .322, 1. } }, -- class K
      { 700,   { 1.00, .960, .404, 1. } }, -- class G
      { 1500,  { 1.00, 1.00, 1.00, 1. } }, -- class F
      { 3000,  { .792, .808, .996, .9 } }, -- class A
      { 8000,  { .545, .588, 1.00, .8 } }, -- class B
      { 17000, { .392, .431, 1.00, .7 } }, -- class O
   }

   -- Constructor
   function point(x,y, weight)
      -- print('weight=',weight,'log=',math.log(weight))
      local pt = {
	 x = x,
	 y = y,
	 size = math.log(weight) * 2,
	 weight = weight,
	 colour = gradient(main_sequence, weight),

	 key = 'pt'..unique(),

	 age = 0,
	 state = state_new,
      }

      setmetatable(pt, _point)

      local bias = .55
      pt.colour = {
	 bias + pt.colour[1] * (1-bias),
	 bias + pt.colour[2] * (1-bias),
	 bias + pt.colour[3] * (1-bias),
	 pt.colour[4] }

      --print('pt.color',unpack(pt.colour))
      return pt
   end
end

--add a new tracked point to the features set
function features:add(idx, x, y, weight)
   pt = point(x, y, weight)

   self[idx] = pt

   --print('add pt.__mesh=',pt.__mesh)
end

vault = heavens()
dying_stars = {}
backdrop = nil

flip = Matrix()
if true then
   -- mirror image
   flip:translate(320,0)
   flip:scale(-1,1)
end

angle = 0
function process_frame(frame)
   --print('Frame!', frame)
 
   local function _inner ()
      if backdrop == nil then
	 -- create a simple mesh for drawing the backdrop
	 backdrop = mesh.new()
	 
	 local topcol = {.04, .07, .51, 1}
	 local botcol = {  0, .58, .99, 1}
	 local w = frame.width
	 local h = frame.height

	 backdrop:add({x=0, y=0, colour=topcol})
	 backdrop:add({x=w, y=0, colour=topcol})
	 backdrop:add({x=0, y=h, colour=botcol})
	 backdrop:add({x=w, y=h, colour=botcol})
      end

      avg = { dx = 0, dy = 0, count = 0 }
      track:track(features)

      if avg.count > 0 then
	 avg.dx = avg.dx / avg.count
	 avg.dy = avg.dy / avg.count
      end

      -- draw backdrop
      gfx.setstate{ blend='none' }
      backdrop:draw(frame)

      features:foreach('update')
      table.foreach(dying_stars, function (k,v) v:update() end)

      -- try to construct a new constellation
      vault:make_constellation()

      -- draw all the constellations + stars
      gfx.setstate{ colour={ .5,.5,0.,.5 }, blend='alpha' }
      vault:draw()
      features:foreach('draw')
      table.foreach(dying_stars, function (k,v) v:draw() end)

      -- thin things out a bit
      local r = math.random()

      if r < .002 then
	 vault:cull(.7)
      elseif r < .1 then
	 vault:cull(.01)
      end
   end

   xform.load(flip, _inner)

   -- drawmemuse(frame)
end
