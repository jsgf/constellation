-- Useful common functions and definitions

features = {}

function features.foreach(self, func)
   for i in self do
      if type(i) == 'number' then
	 p = features[i]
	 p[func](p)
      end
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

function drawframe(frame)
   gfx.setstate({colour={}, blend='none'})
   gfx.sprite(frame, frame.width/2, frame.height/2)
end
