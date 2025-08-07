#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

char *extract_value(const char *msg, const char *key)
{
  // Build search pattern: "\"key\":"
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);

  const char *start = strstr(msg, pattern);
  if (!start)
    return NULL;

  start += strlen(pattern);

  // Skip whitespace and opening quote if present
  while (*start == ' ' || *start == '\"')
    start++;

  const char *end = start;
  // Value ends at quote, comma, or closing brace
  while (*end && *end != '\"' && *end != ',' && *end != '}')
    end++;

  int len = end - start;
  if (len <= 0)
    return NULL;

  char *value = (char *)malloc(len + 1);
  if (!value)
    return NULL;

  strncpy(value, start, len);
  value[len] = '\0';
  return value;
}

int main()
{
  const char *msg = "{\"cmd\":\"start\",\"seq\":0,\"delay_ms\":10000,\"session\":42,\"ack_ip\":\"192.168.1.100\",\"target\":\"all\"}";
  char *cmd = extract_value(msg, "cmd");
  char *seq = extract_value(msg, "seq");
  char *delay_ms = extract_value(msg, "delay_ms");
  char *session = extract_value(msg, "session");
  char *ack_ip = extract_value(msg, "ack_ip");
  char *target = extract_value(msg, "target");

  printf("Extracted values:\n");
  printf("cmd: %s\n", cmd);
  printf("seq: %s\n", seq);
  printf("delay_ms: %s\n", delay_ms);
  printf("session: %s\n", session);
  printf("ack_ip: %s\n", ack_ip);
  printf("target: %s\n", target);

  return 0;
}