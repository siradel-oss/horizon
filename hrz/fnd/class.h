#pragma once

#define HRZ_DEFAULT_COPY(TYPE)   \
    TYPE(const TYPE&) = default; \
    TYPE& operator=(const TYPE&) = default

#define HRZ_DEFAULT_MOVE(TYPE) \
    TYPE(TYPE&&) = default;    \
    TYPE& operator=(TYPE&&) = default

#define HRZ_DEFAULT_COPY_MOVE(TYPE) \
    HRZ_DEFAULT_COPY(TYPE);         \
    HRZ_DEFAULT_MOVE(TYPE)

#define HRZ_DELETE_COPY(TYPE)   \
    TYPE(const TYPE&) = delete; \
    TYPE& operator=(const TYPE&) = delete

#define HRZ_DELETE_MOVE(TYPE) \
    TYPE(TYPE&&) = delete;    \
    TYPE& operator=(TYPE&&) = delete

#define HRZ_DELETE_COPY_MOVE(TYPE) \
    HRZ_DELETE_COPY(TYPE);         \
    HRZ_DELETE_MOVE(TYPE)
