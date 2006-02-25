-- Useful common functions and definitions

features = {}

function features:points()
   local ret = {}

   for i in self do
      if type(i) == 'number' then
	 ret[i] = self[i]
      end
   end
   return ret
end

function features:foreach(func)
   for _,p in self:points() do
      p[func](p)
   end
end

function lerp(p, v1, v2)
   if v1 == nil or v2 == nil then
      return nil
   else
      return v1 + (v2-v1) * p
   end
end

function lerpcol(p, c1, c2)
   return { 
      lerp(p, c1[1], c2[1]),
      lerp(p, c1[2], c2[2]),
      lerp(p, c1[3], c2[3]),
      lerp(p, c1[4], c2[4])
   }
end

-- given a gradient, return a colour for a particular point along it
function gradient(cols, temp)
   local pw, pc			--prev values

   -- This could probably be a binary search or something, if it
   -- seemed worthwhile
   for _,col in cols do
      local w,c = unpack(col)	--weight, colour

      if pw then
	 -- if temp is between this entry and the previous, then
	 -- interpolate it
	 if temp > pw and temp <= w then
	    local p = (temp - pw) / (w-pw)
	    return lerpcol(p, pc, c)
	 end
      else
	 -- if temp is before the first entry, then just return the
	 -- first entry
	 if temp <= w then
	    return c
	 end
      end
      pw,pc = w,c
   end

   -- if we ran out of list, just return the last colour
   return pc
end

function clamp(n, min, max)
   if n < min then
      n = min
   else 
      if n > max then
	 n = max
      end
   end
   return n
end

function dist(p1, p2)
   local dx,dy = p1.x-p2.x,p1.y-p2.y
   return math.sqrt(dx*dx+dy*dy)
end


function drawframe(frame)
   gfx.setstate({colour={}, blend='none'})
   gfx.sprite({x=frame.width/2, y=frame.height/2}, nil, frame)
end

-- a source of unique numeric identifiers
do
   local count=0

   function unique()
      count = count+1
      return count
   end
end

-- round n up to m
function roundup(n, m)
   return math.ceil(n/m)*m
end

-- apply f to each element of list, returning a new list
function map(f, list)
   local ret={}

   for k,v in pairs(list) do
      ret[k] = f(k,v)
   end

   return ret
end

function filter(f, list)
   local ret={}
   for k,v in pairs(list) do
      if f(k,v) then
	 ret[k] = v
      end
   end
   return ret
end

-- draw a graph of memory use
do
   local history={}
   local memmax=100		--initial scale
   local tgtmemmax=memmax
   local maxhist=200		--size of sample history

   function drawmemuse(frame) 
      local info={ gcinfo() }

      tgtmemmax = roundup(math.max(tgtmemmax, info[2]), 100)
      memmax = memmax + (tgtmemmax-memmax) * .1

      local transform = translate(0, frame.height) * scale(frame.width / maxhist,
							   -frame.height / (3*memmax))

      table.insert(history, info)
      if table.getn(history) > maxhist then
	 table.remove(history, 1)
      end

      local function dodraw()
	 -- draw scale
	 gfx.setstate{colour={.5,.5,.5,.5}, blend='alpha'}
	 for y = 0,tgtmemmax,100 do
	    gfx.line({y=y}, {x=maxhist, y=y})
	 end
	 gfx.setstate{colour={1, 1, 1, 1}, blend='none'}
	 gfx.line({y=tgtmemmax}, {x=maxhist, y=tgtmemmax})
	 
	 -- actual memory use
	 gfx.setstate{colour={0,1,1,1}, blend='none'}
	 gfx.line(unpack(map(function (k,v) return {x=k, y=v[1]} end, history)))
	 -- gc threshold
	 gfx.setstate{colour={1,1,0,1}}
	 gfx.line(unpack(map(function (k,v) return {x=k, y=v[2]} end, history)))      
      end

      xform.load(transform, dodraw)
   end
end

-- Simple transforms
-- This is a 2x3 matrix for 2D affine transforms
-- [ a b x
--   c d y ]

Matrix = {}
Matrix.__index = Matrix
setmetatable(Matrix, {
		__call=function (fn,...)
			  local m={} 
			  setmetatable(m,Matrix)
			  m:init(unpack(arg))
			  return m
		       end
	     })

function Matrix:init(...)
   local a,b,c,d,x,y = unpack(arg)

   if type(a) == 'table' then
      b = a.b
      c = a.c
      d = a.d
      x = a.x
      y = a.y
      a = a.a
   end

   self.a = a or 1
   self.b = b or 0
   self.c = c or 0
   self.d = d or 1
   self.x = x or 0
   self.y = y or 0
end

function Matrix:translate(tx,ty)
   self.x = self.a * tx + self.c * ty + self.x
   self.y = self.b * tx + self.d * ty + self.y

   return self
end

function Matrix:scale(x,y)
   self.a = self.a * x
   self.b = self.b * x
   self.c = self.c * y
   self.d = self.d * y

   return self
end

function Matrix:rotate(a)
   local s = math.sin(a)
   local c = math.cos(a)

   local m = Matrix(c, -s, s, c, 0, 0)

   self:mult(m)

   return self
end

function Matrix:mult(m)
   self:init(self * m)

   return self
end

function Matrix:invert()
   local det = self.a*self.d - self.b*self.c

   local a =  self.d / det
   local b = -self.b / det
   local c = -self.c / det
   local d =  self.a / det

   self.a = a
   self.b = b
   self.c = c
   self.d = d

   local t = {x=self.x, y=self.y}
   self.x = 0
   self.y = 0

   self:transform(t)
   self.x = -t.x
   self.y = -t.y

   return self
end

function Matrix:transform(pt)
   local x = pt.x * self.a + pt.y * self.b + self.x
   local y = pt.x * self.c + pt.y * self.d + self.y

   pt.x = x
   pt.y = y

   return pt
end

function Matrix.__mul(A, B)
   local a = A.a * B.a + A.b * B.c
   local b = A.a * B.b + A.b * B.d
   local c = A.c * B.a + A.d * B.c
   local d = A.c * B.b + A.d * B.d
   local x = A.a * B.x + A.b * B.y + A.x
   local y = A.c * B.x + A.d * B.y + A.y
   
   return Matrix(a,b,c,d,x,y)
end

function Matrix:__tostring()
   return string.format('[%g,%g,%g,%g,%g,%g]', self.a, self.b, self.c, self.d, self.x, self.y)
end

-- Matrix+Vector = translation
function Matrix:__add(v)
   return self * translate(v.x, v.y)
end

-- simple helpers to hide the Matrix class
function rotate(a)
   return Matrix():rotate(a)
end

function scale(x,y)
   y = y or x			-- so that scale(x) does something useful
   return Matrix{a=x,d=y}
end

function translate(x,y)
   return Matrix{x=x,y=y}
end
