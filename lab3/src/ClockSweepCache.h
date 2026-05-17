#ifndef CLOCKSWEEPCACHE_H
#define CLOCKSWEEPCACHE_H

#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdio>

typedef unsigned int uint;

template<class ValueT>
class ClockSweepCache
{
public:
	ClockSweepCache()
	{
		m_JobRunning = true;
		m_Job = std::make_unique<std::thread>(&ClockSweepCache::checkAndEvict, this, std::ref(m_JobRunning));
	}

	~ClockSweepCache()
	{
		m_JobRunning = false;
		m_Job->join();
	}

	[[nodiscard]] std::optional<ValueT> get(int key)
	{
		if (m_Map.find(key) != m_Map.end())
		{
			m_Map[key].frequency++;
			return m_Map[key].value;
		} else {
			return std::nullopt;
		}
	}

	void put(int key, ValueT value)
	{
		m_Map[key] = {value, 1};
	}

	void remove(int key)
	{
		m_Map.erase(key);
	}

private:

	struct Data {
		ValueT value;
		int frequency;
	};

	std::unordered_map<int, Data> m_Map;
	bool m_JobRunning;
	std::unique_ptr<std::thread> m_Job;

	void checkAndEvict(const bool &jobRunning)
	{
		using namespace std::chrono_literals;

		while (true)
		{
			if (!jobRunning) return;

			std::vector<int> toRemove;
			for (auto &[key, data] : m_Map)
			{
				data.frequency--;

				if (data.frequency == 0)
				{
					toRemove.emplace_back(key);
				}
			}

			for (int key: toRemove)
			{
				#ifdef LOGGING
				printf("Evicting %d\n", key);
				#endif

				m_Map.erase(key);
			}

			std::this_thread::sleep_for(1s);
		}
	}
};

#endif
