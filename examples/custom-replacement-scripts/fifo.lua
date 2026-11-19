-- FIFO in Lua: the same policy as the built-in FIFO, for checking the custom-policy path.
-- Prefer an invalid line; otherwise evict the line filled earliest (smallest insertTime).
-- Hits do not change the order.

function chooseVictim(lines, request, cache)
    for i, line in ipairs(lines) do
        if not line.valid then
            return i
        end
    end

    local victim = 1
    for i = 2, #lines do
        if lines[i].insertTime < lines[victim].insertTime then
            victim = i
        end
    end
    return victim
end
