package com.example.minidb.optimizer;

import com.example.minidb.index.IndexManager;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class OptimizerTest {

    @Test
    void testIndexChosenWhenAvailable() {

        IndexManager indexManager =
                new IndexManager();

        indexManager.insert(
                1,
                10
        );

        Optimizer optimizer =
                new Optimizer(
                        indexManager
                );

        QueryPlan plan =
                optimizer.choosePlan(
                        true,
                        100,
                        1
                );

        assertEquals(
                QueryPlan.Type.INDEX_SCAN,
                plan.getType()
        );
    }
}