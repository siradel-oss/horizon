// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>
#include <proj_lite.h>

class CrsDatabaseTest : public ::testing::Test
{
protected:
    void SetUp() override { crs_db = pl_load_crs_database(); }

    void TearDown() override { pl_destroy_crs_database(crs_db); }

    pl_CrsDatabase* crs_db;
};
