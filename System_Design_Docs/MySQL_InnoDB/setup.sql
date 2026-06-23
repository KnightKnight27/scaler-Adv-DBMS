DROP TABLE IF EXISTS orders;

CREATE TABLE orders (
    order_id BIGINT PRIMARY KEY,
    customer_id BIGINT NOT NULL,
    amount DECIMAL(10, 2) NOT NULL,
    status VARCHAR(20) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_customer_status (customer_id, status)
) ENGINE = InnoDB;

INSERT INTO orders (order_id, customer_id, amount, status) VALUES
    (1001, 10, 499.00, 'PAID'),
    (1002, 10, 1299.00, 'PENDING'),
    (1003, 11, 299.00, 'PAID'),
    (1004, 12, 799.00, 'CANCELLED');

EXPLAIN SELECT * FROM orders WHERE customer_id = 10 AND status = 'PAID';
