#ifndef SANCOV_SEC_H
#define SANCOV_SEC_H

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct SancovEntry {
  void *func;
  void *guard;
} __attribute__((packed));

struct SancovCfgEdge {
  void *src;
  void *dst;
} __attribute__((packed));

struct SancovFuncCall {
  void *guard;
  void *func;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // SANCOV_SEC_H
