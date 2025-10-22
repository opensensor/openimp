# Rate Limit Handling

The agent now handles OpenAI API rate limits automatically with intelligent retry logic.

## What Happens

When you hit a rate limit:

```
[2/20] Analyzing: IMP_IVS_CreateGroup...
  ✓ Function 'IMP_IVS_CreateGroup' EXISTS in OEM binary port_9009
  ⏳ Rate limit hit, waiting 4.5s (attempt 1/5)...
  → Found 3 struct offsets
  → Generated corrected implementation
```

## Retry Strategy

### 1. Automatic Retry
- Up to **5 attempts** per function
- Exponential backoff: 1s, 2s, 4s, 8s, 16s
- Extracts wait time from OpenAI error message when available

### 2. Smart Wait Times
If OpenAI says "try again in 3.508s":
- Agent waits **4.5s** (adds 1s buffer)

If no wait time specified:
- Uses exponential backoff: 2^attempt seconds

### 3. Between-Request Delay
- **0.5s delay** between successful requests
- Prevents hitting rate limits in the first place

## Example Output

### Success After Retry
```
[5/20] Analyzing: IMP_IVS_CreateChn...
  ✓ Function 'IMP_IVS_CreateChn' EXISTS in OEM binary port_9009
  ⏳ Rate limit hit, waiting 4.5s (attempt 1/5)...
  → Found 2 struct offsets
  → Generated corrected implementation
```

### Multiple Retries
```
[8/20] Analyzing: IMP_IVS_StartRecvPic...
  ✓ Function 'IMP_IVS_StartRecvPic' EXISTS in OEM binary port_9009
  ⏳ Rate limit hit, waiting 4.5s (attempt 1/5)...
  ⏳ Rate limit hit, waiting 8.0s (attempt 2/5)...
  → Found 1 struct offsets
  → Generated corrected implementation
```

### Failure After Max Retries
```
[12/20] Analyzing: IMP_IVS_PollingResult...
  ✓ Function 'IMP_IVS_PollingResult' EXISTS in OEM binary port_9009
  ⏳ Rate limit hit, waiting 4.5s (attempt 1/5)...
  ⏳ Rate limit hit, waiting 8.0s (attempt 2/5)...
  ⏳ Rate limit hit, waiting 16.0s (attempt 3/5)...
  ⏳ Rate limit hit, waiting 32.0s (attempt 4/5)...
  ✗ Error: Rate limit reached for gpt-4o...
  [Skipping this function]
```

## Rate Limit Details

### OpenAI Limits (gpt-4o)
- **30,000 tokens per minute** (TPM)
- Each function analysis uses ~5,000-10,000 tokens
- Can process ~3-6 functions per minute

### Calculation
With 0.5s delay between requests:
- 2 requests per second max
- 120 requests per minute max
- But token limit is the real constraint

## Tips

### 1. Monitor Progress
The agent shows you:
- Which function is being analyzed
- When rate limits are hit
- How long it's waiting
- Progress through the file

### 2. Let It Run
The agent will automatically:
- Wait when needed
- Retry failed requests
- Continue processing

### 3. Check Your Quota
Visit: https://platform.openai.com/account/rate-limits

You can see:
- Current usage
- Rate limits
- Quota remaining

### 4. Upgrade If Needed
If you're hitting limits frequently:
- Upgrade to higher tier (Tier 2+)
- Tier 2: 450,000 TPM (15x faster)
- Tier 3: 10,000,000 TPM (333x faster)

## Error Messages

### Rate Limit Error
```
Error code: 429 - {'error': {'message': 'Rate limit reached for gpt-4o in organization org-XXX on tokens per min (TPM): Limit 30000, Used 3494, Requested 28260. Please try again in 3.508s.'}}
```

**What it means:**
- You've used 3,494 tokens
- This request needs 28,260 tokens
- Total would be 31,754 (over 30,000 limit)
- Wait 3.5 seconds for tokens to reset

**Agent handles it:**
- Waits 4.5s (3.5s + 1s buffer)
- Retries the request
- Continues processing

### Other Errors
Non-rate-limit errors (network, API issues):
- Agent shows error
- Skips the function
- Continues with next function

## Performance

### Typical Run
Processing 100 functions:
- ~50 functions/hour (with rate limits)
- ~2 hours for full codebase
- Automatic retries handle transient issues

### Without Rate Limits
With higher tier:
- ~200 functions/hour
- ~30 minutes for full codebase

## Summary

The agent now:
- ✅ Automatically retries on rate limits (up to 5 times)
- ✅ Uses smart wait times from OpenAI error messages
- ✅ Adds 0.5s delay between requests to prevent limits
- ✅ Shows clear progress and wait times
- ✅ Continues processing even if some functions fail

You can just run it and let it handle rate limits automatically!

