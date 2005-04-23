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

-- draw a graph of memory use
do
   local history={}
   local memmax=100		--initial scale
   local tgtmemmax=memmax
   local maxhist=200		--size of sample history

   function drawmemuse(frame) 
      local xscale=frame.width / maxhist
      local yscale=(frame.height / 3) / memmax

      local info={ gcinfo() }
      local function coord(x,y)
	 return { x=x*xscale, y=frame.height-y*yscale }
      end

      tgtmemmax = roundup(math.max(tgtmemmax, info[2]), 100)
      memmax = memmax + (tgtmemmax-memmax) * .1

      table.insert(history, info)
      if table.getn(history) > maxhist then
	 table.remove(history, 1)
      end

      -- draw scale
      gfx.setstate{colour={.5,.5,.5,.5}, blend='alpha'}
      for y = 0,tgtmemmax,100 do
	 gfx.line(coord(0, y), coord(maxhist, y))
      end
      gfx.setstate{colour={1, 1, 1, 1}, blend='none'}
      gfx.line(coord(0, tgtmemmax), coord(maxhist, tgtmemmax))

      -- actual memory use
      gfx.setstate{colour={0,1,1,1}, blend='none'}
      gfx.line(unpack(map(function (k,v) return coord(k, v[1]) end, history)))
      -- gc threshold
      gfx.setstate{colour={1,1,0,1}}
      gfx.line(unpack(map(function (k,v) return coord(k, v[2]) end, history)))      
   end
end

