#pragma once

#include <string>
#include <unordered_set>
#include <condition_variable>
#include <mutex>
#include <pqxx/pqxx>
#include <queue>

#include <iostream>

namespace cp
{

	struct named_query;
	struct connection_pool;
	struct connection_manager;
	struct query;

	std::string serialize(pqxx::result res);
	std::string serialize(pqxx::row res);

	struct basic_connection final {
		basic_connection(connection_pool& pool);

		~basic_connection();

		pqxx::connection& get() const;

		operator pqxx::connection&();
		operator const pqxx::connection&() const;

		pqxx::connection* operator->();
		const pqxx::connection* operator->() const;

		void prepare(std::string_view name, std::string_view definition);

		basic_connection(const basic_connection&) = delete;
		basic_connection& operator=(const basic_connection&) = delete;

	private:
		connection_pool& pool;
		std::unique_ptr<connection_manager> manager;
	};

	struct basic_transaction {
		void prepare_one(const query& q);

		void prepare_one(const named_query& q);


		template<typename... Queries>
		void prepare(Queries&&... queries) {
			(prepare_one(std::forward<Queries>(queries)), ...);
		}

		template<typename... Queries>
		basic_transaction(connection_pool& pool, Queries&&... queries) : connection(pool), transaction(connection.get()) {
			prepare(std::forward<Queries>(queries)...);
		}

		basic_transaction(const basic_transaction&) = delete;
		basic_transaction& operator=(const basic_transaction&) = delete;

		pqxx::result exec(std::string_view q);
		void commit();
		void abort();

		pqxx::work& get();
		operator pqxx::work&();

		friend struct query_manager;
		friend struct connection_pool;

	private:
		basic_connection connection;
		pqxx::work transaction;
	};


	struct connection_manager {
		connection_manager(std::unique_ptr<pqxx::connection>& connection);
		connection_manager(const connection_manager&) = delete;
		connection_manager& operator=(const connection_manager&) = delete;

		void prepare(const std::string& name, const std::string& definition);
		friend struct basic_connection;

	private:
		std::unordered_set<std::string> prepares{};
		std::mutex prepares_mutex{};
		std::unique_ptr<pqxx::connection> connection{};
	};


    struct connection_options {
		std::string dbname{};
		std::string user{};
		std::string password{};
		std::string hostaddr{};
		int16_t port = 5432;

		int connections_count = 8;
	};


	struct 	connection_pool {
		connection_pool(const connection_options& options);

		std::unique_ptr<connection_manager> borrow_connection();

		void return_connection(std::unique_ptr<connection_manager>& manager);

	private:
		std::mutex connections_mutex{};
		std::condition_variable connections_cond{};
		std::queue<std::unique_ptr<connection_manager>> connections{};
	};

	struct query_manager {
		query_manager(basic_transaction& transaction, std::string_view query_id);

		template<typename... Args>
		pqxx::result exec_prepared(Args&&... args);

	private:
		std::string query_id{};
		basic_transaction& transaction_view;
	};
    

	struct query {
		query(std::string_view str);

		const char* data() const;

		operator std::string() const;

		constexpr operator std::string_view() const;

		template<typename... Args>
		pqxx::result operator()(Args&&... args) {
			return exec(std::forward<Args>(args)...);
		}

		template<typename... Args>
		pqxx::result exec(Args&&... args) {
			if (!manager.has_value())
				throw std::runtime_error("attempt to execute a query without connection with a transaction");
			return manager->exec_prepared(std::forward<Args>(args)...);
		}

		friend struct query_manager;
		friend struct basic_transaction;

	protected:
		std::string str;
		mutable std::optional<query_manager> manager{};

    };



	template<typename... Args>
	pqxx::result query_manager::exec_prepared(Args&&... args) {
		return transaction_view.transaction.exec_prepared(query_id, std::forward<Args>(args)...);
	}

    template<typename... Queries>
	basic_transaction tx(connection_pool& pool, Queries&&... queries) {
		return basic_transaction(pool, std::forward<Queries>(queries)...);
	}
}
