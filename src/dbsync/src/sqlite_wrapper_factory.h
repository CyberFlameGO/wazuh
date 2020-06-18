/*
 * Wazuh DBSYNC
 * Copyright (C) 2015-2020, Wazuh Inc.
 * June 11, 2020.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#pragma once
#include "sqlite_wrapper.h"

class ISQLiteFactory {
public:
  virtual std::shared_ptr<SQLite::IConnection> CreateConnection(const std::string& path) = 0;
  virtual std::unique_ptr<SQLite::ITransaction> CreateTransaction(std::shared_ptr<SQLite::IConnection>& connection) = 0;
  virtual std::unique_ptr<SQLite::IStatement> CreateStatement(std::shared_ptr<SQLite::IConnection>& connection, const std::string& query) = 0;
};

class SQLiteFactory : public ISQLiteFactory {
public:
  SQLiteFactory() = default;
  virtual ~SQLiteFactory() = default;
  
  std::shared_ptr<SQLite::IConnection> CreateConnection(const std::string& path) override {
    return std::make_shared<SQLite::Connection>(path);
  }
  std::unique_ptr<SQLite::ITransaction> CreateTransaction(std::shared_ptr<SQLite::IConnection>& connection) override {
    return std::make_unique<SQLite::Transaction>(connection);
  }
  
  std::unique_ptr<SQLite::IStatement> CreateStatement(std::shared_ptr<SQLite::IConnection>& connection, const std::string& query) override {
    return std::make_unique<SQLite::Statement>(connection, query);
  }
private:
  
  SQLiteFactory(const SQLiteFactory&) = delete;
  SQLiteFactory& operator=(const SQLiteFactory&) = delete;
};

